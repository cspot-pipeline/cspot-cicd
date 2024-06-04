#!/usr/bin/env python3
import argparse
import json
import os
import requests
import sys
import time

import boto3
import botocore.config
import botocore.exceptions


class Env:
	"""
	Simple wrapper around variables that come from shell environment
	"""
	defaults = {
		'REGION': 'eucalyptus',
		'AMI_IMAGE': 'ami-97b07ba03fe3f6a93',
		'GITHUB_OUTPUT': 'gh_out.txt',
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
		self.EC2 = ec2_client
		instance = self.EC2.run_instances(**run_args, MaxCount=1, MinCount=1)['Instances'][0]
		self.__dict__.update(instance)
		self.InstanceId = instance['InstanceId']  # redundant, but make this one explicit

	def get_state(self) -> str:
		return self.EC2.describe_instances(InstanceIds=[self.InstanceId])['Reservations'][0]['Instances'][0]['State'][
			'Name']

	def terminate(self):
		"""
		Terminates the instance
		"""
		self.EC2.terminate_instances(InstanceIds=[self.InstanceId])


class GHActionsRunner(dict):
	"""
	Wrapper class to represent a self-hosted runner on GitHub actions
	"""

	def __init__(self):
		super().__init__()


class Main:
	def __init__(self, os_env):
		self.ENV = Env(os_env)
		conf = botocore.config.Config(region_name='eucalyptus')
		self.EC2 = boto3.client('ec2', config=conf)

	def create_instance(self, keyname: str) -> EC2Instance:
		"""
		Creates an instance on Eucalyptus
		:param keyname: name of SSH key to use
		:return: instance info as JSON object
		"""
		print('Creating instance...')
		ec2_instance = EC2Instance(self.EC2, KeyName=keyname, ImageId=self.ENV.AMI_IMAGE, InstanceType='t2.xlarge')

		instance_id = ec2_instance.InstanceId
		public_ip = ec2_instance.PublicIpAddress
		print(f'Created new instance with ID {instance_id} at public IP {public_ip}.')

		start_time = time.time()
		while time.time() - start_time < self.ENV.EC2_CREATE_TIMEOUT:
			print('Checking if instance is live...', end='')
			if 'running' in ec2_instance.get_state():
				print('Yes!')
				break
			print('No.')
			time.sleep(5)
		else:  # code here will only be executed if exited the loop *without* breaking
			print("Error: Timed out waiting for instance to come online", file=sys.stderr)
			ec2_instance.terminate()

		with open(self.ENV.GITHUB_OUTPUT, 'a+') as gh_output:
			gh_output.write(f'CURR_ID={instance_id}\n')
			gh_output.write(f'PUBLIC_IP={public_ip}\n')

		return ec2_instance

	def add_runner(self, inst_id: str, gh_acc_token: str) -> str:
		"""
		Adds an AWS instance to the repo's list of self-hosted runners
		:param inst_id: AWS instance ID
		:param gh_acc_token: GitHub personal access token to use for HTTPS requests
		:return: Token to be used in the runner configuration
		"""

	def close_client(self):
		self.EC2.close()


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
		sys.exit(1)  # abort if instance creation failed

	# wait for CI pipeline to complete and shut down the system
	while main_instance.get_state() != 'stopped':
		time.sleep(5)

	# main.delete_runner()

	print("Cleaning up")
	main_instance.terminate()
	main.close_client()
