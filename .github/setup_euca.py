#!/usr/bin/env python3.12
import argparse
import atexit
import json
import os
import sys
import time
from stat import S_IRUSR
from typing import Callable
from uuid import uuid4

import boto3
import botocore.exceptions
import paramiko
import requests


class Env:
	"""
	Simple wrapper around variables that come from OS environment. Order of precedence (highest to lowest):
		- local .env file
		- OS environment
		- default values (configured below)

	Default values are NOT propagated back to the outer environment (although ones loaded from an .env file *are*).
	"""
	defaults = {
		'AWS_DEFAULT_REGION': 'eucalyptus',
		'AMI_IMAGE': 'ami-f5412b12d9ad4af01',
		'REPO_URL': 'https://github.com/cspot-pipeline/cspot-cicd',
		'GH_ACCESS_TOKEN': '',
		'CICD_SSH_KEY': '',
		'EC2_CREATE_TIMEOUT': '300',  # time to wait in seconds before aborting create operation
		'DEFAULT_RUNNER_LABELS': 'self-hosted,Linux,x86_64',
	}

	def __init__(self):
		os_env = os.environ
		# overwrite os environment values with ones from .env file, if present
		Env.load_dotenv(os_env)
		props = Env.defaults
		for p in props:
			props[p] = os_env.get(p, props[p])
		self.__dict__.update(props)

	# print(self.__dict__)

	def update(self, new: dict[str, str]):
		self.__dict__.update(new)

	@classmethod
	def load_dotenv(cls, os_env: dict[str, str], filepath: str = '.env'):
		print(f'Checking for environment file at {filepath}')
		try:
			envfile = open(filepath, 'r')
		except FileNotFoundError:
			print('No env file found, using OS environment')
			return

		for line in envfile:
			line = line.strip()
			if line.startswith('#') or '=' not in line:
				continue
			name, val = (i.strip() for i in line.split('=', maxsplit=1))
			os_env[name] = val
		print(f'Loaded environment configuration from file {filepath}')
		envfile.close()


class EC2Instance(dict):
	"""
	Wrapper class to represent an EC2 instance on Eucalyptus
	"""

	def __init__(self, ec2_client, **run_args):
		super().__init__()
		self.should_terminate = False
		self.EC2 = ec2_client
		instance = self.EC2.run_instances(**run_args, MaxCount=1, MinCount=1)['Instances'][0]
		self.should_terminate = True
		self.__dict__.update(instance)
		self.InstanceId = instance['InstanceId']  # redundant, but make this one explicit

	def get_state(self) -> str:
		return self.EC2.describe_instances(InstanceIds=[self.InstanceId])['Reservations'][0]['Instances'][0]['State'][
			'Name']

	def set_shutdown_behavior(self, behavior: str):
		self.EC2.modify_instance_attribute(InstanceId=self.InstanceId,
										   InstanceInitiatedShutdownBehavior={'Value': behavior})

	def terminate(self):
		"""
		Terminates the instance
		"""
		if not self.should_terminate:
			return
		self.EC2.terminate_instances(InstanceIds=[self.InstanceId])
		print(f'Terminated instance with id {self.InstanceId}')
		self.should_terminate = False

	def __del__(self):
		self.terminate()


def _check_response(resp: requests.Response):
	if not resp.ok:
		print_error(f"error in HTTP request: status {resp.status_code}")
		exit(1)


class GHActionsRunner(dict):
	"""
	Wrapper class to represent a self-hosted runner on GitHub actions
	"""
	API_URL = 'https://api.github.com/repos/cspot-pipeline/cspot-cicd/actions/runners'

	def __init__(self, pat_token: str, labels: list[str],
				 name: str = '', runner_group: int = 1):
		"""
		:param pat_token: GitHub personal access token value
		:param labels: list of labels to be attached to the runner.
		:param name: (optional) name for the runner to use
		:param runner_group: (optional) group number to place the new runner in
		"""
		super().__init__()

		self.headers = {
			'X-GitHub-Api-Version': '2022-11-28',
			'accept': 'application/vnd.github+json',
		}
		self.auth = (None, pat_token)

		# grab a registration token that will allow us to configure new runners once added
		response = requests.post(GHActionsRunner.API_URL + '/registration-token',
								 auth=self.auth, headers=self.headers)
		_check_response(response)
		token_json = json.loads(response.text)
		self.registration_token = token_json['token']
		print(f'Got GitHub actions registration token with expiration of {token_json["expires_at"]}')

		# get the number of existing runners and (if applicable) check whether the requested name is available
		response = requests.get(GHActionsRunner.API_URL, auth=self.auth, headers=self.headers)
		_check_response(response)

		registered_runners = json.loads(response.text)
		actual_name = f'cspot-runner-{uuid4()}'
		if name != '':
			for r in registered_runners['runners']:
				if name == r['name']:
					print_error(f'A runner with name "{name}" already exists. Using "{actual_name}" instead.')
					break
			else:  # note this is paired with the for loop, NOT the if block (not a typo)
				actual_name = name

		self.name = actual_name
		self.runner_group_id = runner_group
		self.labels = labels

	def configure(self, host: paramiko.SSHClient, url: str):
		"""
		runs the configuration on the remote host
		:param host: the host (should already be connected)
		:param url: URL of the repo on GitHub
		"""
		print('Downloading...')
		tarball = 'actions-runner.tar.gz'
		_, stdo, _ = host.exec_command(f'curl -o {tarball} -L {self.get_download_url()}')
		if stdo.channel.recv_exit_status() != 0:
			print_error('runner configuration failed: unable to download runner application')
			exit(1)

		print('Extracting...')
		_, stdo, _ = host.exec_command(f'tar xzf {tarball}')
		if stdo.channel.recv_exit_status() != 0:
			print_error('runner configuration failed: unable to extract application tarball')
			exit(1)

		print('Running configure script...')
		conf_cmd = './config.sh --unattended --no-default-labels --ephemeral ' \
				   f'--url {url} --token {self.registration_token} ' \
				   f'--name {self.name} ' \
				   f'--labels {",".join(self.labels)}'
		print(conf_cmd)

		# note: can add --ephemeral flag below to have the runner auto-delete itself after 1 job
		_, stdo, stde = host.exec_command(conf_cmd)
		if stdo.channel.recv_exit_status() != 0:
			print_error('runner configuration failed: configuration script exited abnormally:\n'
						+ stde.read().decode('UTF-8'))
			exit(1)

		print('Launching runner process...')
		_, stdo, _ = host.exec_command('bash -c "nohup ./run.sh &"')
		if stdo.channel.recv_exit_status() != 0:
			print_error('runner configuration failed: configuration script exited abnormally:\n'
						+ stde.read().decode('UTF-8'))
			exit(1)

		# wait for runner to show up on GitHub
		time.sleep(20)

		# add info about ourself to the `self` object
		response = requests.get(GHActionsRunner.API_URL,
								headers=self.headers, auth=self.auth)
		_check_response(response)
		runners = json.loads(response.text)['runners']
		me = {}
		for r in runners:
			if r['name'] == self.name:
				me = r
				break
		else:  # configuration failed--we aren't present in the repo's runners list
			print_error('An unknown error occurred on the runner')
		# exit(1)

		self.update(me)
		self.__dict__.update(me)

	def get_download_url(self) -> str:
		"""
		Get the download URL for the self-hosted runner application
		"""
		response = requests.get(GHActionsRunner.API_URL + '/downloads',
								auth=self.auth, headers=self.headers)
		_check_response(response)
		results = json.loads(response.text)
		for r in results:
			if r['os'] == 'linux' and r['architecture'] == 'x64':
				return r['download_url']
		print_error('Unable to find download URL for (linux, x64) runner')

	def is_autoremoved(self) -> bool:
		"""
		[For ephemeral runners] checks whether the runner has deregistered itself after completing
		its job
		"""

		response = requests.get(GHActionsRunner.API_URL + f'/{self.id}',
								auth=self.auth, headers=self.headers)
		return not response.ok

	def deregister(self):
		"""
		Removes the runner associated with this object from the repo
		"""
		response = requests.delete(GHActionsRunner.API_URL + f'/{self.id}',
								   auth=self.auth, headers=self.headers)
		# _check_response(response)  # this will trigger an infinite loop if the request fails
		if response.ok:
			print(f'Removed runner "{self.name}" (id {self.id}).')
		# else, assume the runner was already deleted/not present on GitHub


class Main:
	def __init__(self):
		self.ENV = Env()
		self.EC2 = boto3.client('ec2')

	def create_instance(self, keyname: str) -> EC2Instance:
		"""
		Creates an instance on Eucalyptus
		:param keyname: name of SSH key to use
		:return: instance wrapper object
		"""
		print('Creating instance...')
		ec2_instance = EC2Instance(self.EC2, KeyName=keyname, ImageId=self.ENV.AMI_IMAGE, InstanceType='t2.xlarge')

		instance_id = ec2_instance.InstanceId
		public_ip = ec2_instance.PublicIpAddress
		print(f'Created new instance with ID {instance_id} at public IP {public_ip}.')

		start_time = time.time()
		while time.time() - start_time < int(self.ENV.EC2_CREATE_TIMEOUT):
			print('Checking if instance is live...', end='')
			if 'running' in ec2_instance.get_state():
				print('Yes!')
				break
			print('No.')
			time.sleep(5)
		else:  # code here will only be executed if we exited the loop *without* breaking
			print_error("Timed out waiting for instance to come online")
			ec2_instance.terminate()

		# doesn't work
		ec2_instance.set_shutdown_behavior('Terminate')

		return ec2_instance

	def get_ssh_key(self, keyfile: str = '') -> str:
		if keyfile != '':
			return keyfile

		keypath = f'/tmp/keyfile-{uuid4()}'
		with open(keypath, 'w+') as privkey:
			privkey.write(self.ENV.CICD_SSH_KEY)
		os.chmod(keypath, S_IRUSR)

		return keypath

	def start_runner(self, instance: EC2Instance, ssh_key: str = '',
					 runner_name: str = '', **kwargs) -> GHActionsRunner:
		"""
		Configures and starts a self-hosted runner on the specified EC2 instance
		:param instance: EC2 instance to host the runner
		:param ssh_key: (optional) path to existing SSH private key file to use (instead of value from the environment)
		:param runner_name: (optional) name to assign to the runner on GitHub
		:param kwargs: (optional) list of keyword arguments to be forwarded to the GHActionsRunner constructor
		:return: runner object
		"""

		if runner_name == '':
			runner_name = f'cspot-runner-{instance.InstanceId}'
		runner = GHActionsRunner(self.ENV.GH_ACCESS_TOKEN, name=runner_name, **kwargs)

		print('Preparing to connect to runner...')

		# set up SSH key
		keypath = self.get_ssh_key(ssh_key)

		remote = paramiko.SSHClient()
		remote.set_missing_host_key_policy(paramiko.AutoAddPolicy)
		time.sleep(10)
		while True:
			try:
				remote.connect(hostname=instance.PublicIpAddress,
							   username='cloud-user',
							   key_filename=keypath,
							   timeout=20)
			except:
				print('Connection failed, trying again...')
				time.sleep(5)
				continue
			break

		print('Successfully connected to runner over SSH')
		print('Configuring runner')

		runner.configure(remote, self.ENV.REPO_URL)
		print('Configuration confirmed successful. Runner is now listening for jobs.')

		remote.close()
		# hack. want to remove keyfile from the runner but not dev machine; easiest way to determine is this
		if ssh_key == '':
			os.remove(keypath)

		return runner

	def close_client(self):
		self.EC2.close()


class Dummy:
	"""
	Dummy class to act as placeholder in case cleanup() is called before runner/instance
	gets created
	"""

	def deregister(self):
		return

	def terminate(self):
		return


def cleanup(stop_on_error: Callable[[], bool]):
	if stop_on_error():
		print("Script halted before cleanup.")
		# (for debugging on GitHub actions)
		from signal import pause
		try:
			pause()
		except KeyboardInterrupt:  # ignore but continue
			pass

	global main
	global main_instance
	global main_runner
	print("Cleaning up")
	main_instance.terminate()  # needs to be called before the boto client is closed!
	try:
		main_runner.deregister()
	except KeyboardInterrupt:  # not sure why this is the error that gets thrown if the HTTP request fails?
		pass
	main.close_client()


def print_error(*args):
	print(f'{os.path.basename(__file__)}: error:', *args, file=sys.stderr)


if __name__ == "__main__":
	main = Main()

	parser = argparse.ArgumentParser()
	parser.add_argument('key_name', type=str, help='Name of PEM key as used by AWS')
	parser.add_argument('-s', '--local-ssh-key', type=str, nargs='?', const=None, default='',
						help='[Dev machines only] Use an existing SSH key file, rather than grabbing from the environment.\n'
							 '(Optional argument: path to keyfile, omit to use "~/.ssh/<key_name>.pem")')
	parser.add_argument('-l', '--label', type=str, nargs='+', action='extend', default=[],
						help='Label(s) to add to the created runner, in addition to the default.\n'
							 "Required if `--no-default-labels' is present")
	parser.add_argument('--no-default-labels', action='store_true',
						help=f'Omit the default labels ({main.ENV.DEFAULT_RUNNER_LABELS}) from the runner')
	parser.add_argument('--stop-on-error', action='store_true',
						help='(For debugging) Suspend the script immediately before performing'
							 ' cleanup when an error is encountered')
	args = parser.parse_args()

	# Note: GitHub ignores duplicate labels and only adds 1 to the instance
	runner_labels = ([] if args.no_default_labels else main.ENV.DEFAULT_RUNNER_LABELS.split(',')) + args.label
	if len(runner_labels) == 0:
		print_error("The `--label' argument is required if the `--no-default-labels' option is used")
		exit(1)

	sshkey = args.local_ssh_key
	if sshkey is None:
		sshkey = f'{os.environ["HOME"]}/.ssh/{args.key_name}.pem'

	# safely handle early cleanup()
	main_runner = Dummy()
	main_instance = Dummy()
	# need to capture the value of stop_on_error when cleanup is called, NOT when we define this callback
	atexit.register(cleanup, lambda: args.stop_on_error)

	try:
		main_instance = main.create_instance(args.key_name)
	except botocore.exceptions.ClientError as err:
		print_error('failed to create Eucalyptus instance')
		# TODO: print exception's error message
		exit(1)  # abort if instance creation failed

	# try:
	main_runner = main.start_runner(main_instance, ssh_key=sshkey, labels=runner_labels)
	# except:
	# 	print_error('failed to start self-hosted runner')
	# 	exit(1)

	# wait for CI pipeline to complete and shut down the system
	# while main_instance.get_state() != 'stopped':
	# 	time.sleep(30)
	while not main_runner.is_autoremoved():
		print('Waiting for runner job to complete...')
		time.sleep(10)

	print('Done!')
	args.stop_on_error = False
	exit(0)
