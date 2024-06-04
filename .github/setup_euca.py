#!/usr/bin/env python3.12
import argparse
import json
import os
import sys
import time
from stat import S_IRUSR
from uuid import uuid4

import boto3
import botocore.config
import botocore.exceptions
import paramiko
import requests


class Env:
	"""
	Simple wrapper around variables that come from shell environment
	"""
	defaults = {
		'AWS_DEFAULT_REGION': 'eucalyptus',
		'AMI_IMAGE': 'ami-f5412b12d9ad4af01',
		'REPO_URL': 'https://github.com/cspot-pipeline/cspot-cicd',
		'GH_ACCESS_TOKEN': '',
		'CICD_SSH_KEY': '',
		'EC2_CREATE_TIMEOUT': 300  # time to wait in seconds before aborting create operation
	}

	def __init__(self, os_env):
		props = Env.defaults
		for p in props:
			props[p] = os_env.get(p, props[p])
		self.__dict__.update(props)


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
		print(f"error in HTTP request: status {resp.status_code}", file=sys.stderr)
		cleanup()
		exit(1)


class GHActionsRunner(dict):
	"""
	Wrapper class to represent a self-hosted runner on GitHub actions
	"""
	API_URL = 'https://api.github.com/repos/cspot-pipeline/cspot-cicd/actions/runners'

	def __init__(self, pat_token: str, name: str = '', runner_group: int = 1):
		"""
		:param pat_token: GitHub personal access token value
		:param name: (optional) name for the runner to use
		:param runner_group: (optional) group number to place the new runner in
		"""
		super().__init__()

		self.should_deregister = False
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
					print(f'A runner with name "{name}" already exists. Using "{actual_name}" instead.',
						  file=sys.stderr)
					break
			else:  # note this is paired with the for loop, NOT the if block (not a typo)
				actual_name = name

		self.name = actual_name
		self.runner_group_id = runner_group
		self.labels = ['self-hosted', 'x86_64', 'Linux']

	def configure(self, host: paramiko.SSHClient, url: str):
		"""
		runs the configuration on the remote host
		:param host: the host (should already be connected)
		:param url: URL of the repo on GitHub
		"""
		tarball_path = 'actions-runner.tar.gz'
		host.exec_command(f'curl -o {tarball_path} -L {self.get_download_url()}')
		time.sleep(1)
		host.exec_command('tar xzf actions-runner.tar.gz')
		time.sleep(5)
		# note: can add --ephemeral flag below to have the runner auto-delete itself after 1 job
		host.exec_command('./config.sh --unattended --no-default-labels '
						  f'--url {url} --token {self.registration_token} '
						  f'--name {self.name} '
						  f'--labels {",".join(self.labels)} '
						  ' && bash -c "nohup ./run.sh &"')
		# ^^last line is here^^ so we don't have to wrestle with sleep() timings between ./configure and ./run
		time.sleep(45)  # <-- increase to 40-60 if this is too short

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
			print('Configuration failed due to an unknown error', file=sys.stderr)
			cleanup()
			exit(1)

		self.should_deregister = True
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
		print('Unable to find download URL for (linux, x64) runner', file=sys.stderr)

	def deregister(self):
		"""
		Removes the runner associated with this object from the repo
		"""
		if not self.should_deregister:
			return
		response = requests.delete(GHActionsRunner.API_URL + f'/{self.id}',
								   auth=self.auth, headers=self.headers)
		# _check_response(response)  # this will trigger an infinite loop if the request fails
		print(f'Removed runner "{self.name}" (id {self.id}).')
		self.should_deregister = False

	def __del__(self):
		self.deregister()


class Main:
	def __init__(self, os_env):
		self.ENV = Env(os_env)
		conf = botocore.config.Config(region_name='eucalyptus')
		self.EC2 = boto3.client('ec2', config=conf)

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
			print("Error: Timed out waiting for instance to come online", file=sys.stderr)
			ec2_instance.terminate()

		# doesn't work
		ec2_instance.set_shutdown_behavior('Terminate')

		return ec2_instance

	def start_runner(self, instance: EC2Instance, runner_name: str = '') -> GHActionsRunner:
		"""
		Configures and starts a self-hosted runner on the specified EC2 instance
		:param instance: EC2 instance to host the runner
		:param runner_name: (optional) name to assign to the runner on GitHub
		:return: runner object
		"""

		if runner_name == '':
			runner_name = f'cspot-runner-{instance.InstanceId}'
		runner = GHActionsRunner(self.ENV.GH_ACCESS_TOKEN, name=runner_name)

		print('Preparing to connect to runner...')

		# set up SSH key
		keypath = f'/tmp/keyfile-{uuid4()}'
		with open(keypath, 'w+') as privkey:
			privkey.write(self.ENV.CICD_SSH_KEY)
		os.chmod(keypath, S_IRUSR)

		remote = paramiko.SSHClient()
		remote.set_missing_host_key_policy(paramiko.AutoAddPolicy)
		time.sleep(5)
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

		remote.close()
		os.remove(keypath)
		print('Configuration complete. Runner is now listening for jobs.')

		return runner

	def close_client(self):
		self.EC2.close()

	def __del__(self):
		self.close_client()  # doesn't appear to have an issue being called twice


class DummyRunner:
	def deregister(self):
		return


def cleanup():
	global main
	global main_instance
	global main_runner
	print("Cleaning up")
	main_instance.terminate()  # needs to be called before the boto client is closed!
	main_runner.deregister()
	main.close_client()


if __name__ == "__main__":
	parser = argparse.ArgumentParser()
	parser.add_argument('-n', '--key-name', type=str, help='Name of PEM key as used by AWS')
	args = parser.parse_args()

	main = Main(os.environ)

	try:
		main_instance = main.create_instance(args.key_name)
	except botocore.exceptions.ClientError as err:
		print('Error: failed to create Eucalyptus instance', file=sys.stderr)
		# TODO: print exception's error message
		cleanup()
		exit(1)  # abort if instance creation failed

	try:
		main_runner = main.start_runner(main_instance)
	except:
		main_runner = DummyRunner()
		cleanup()  # this will error otherwise since main_runner is not defined
		exit(1)

	# wait for CI pipeline to complete and shut down the system
	while main_instance.get_state() == 'running':
		time.sleep(30)

	cleanup()
