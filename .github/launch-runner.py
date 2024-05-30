#!/usr/bin/env python3

import json, argparse
from subprocess import Popen, check_output, CalledProcessError
import os
from time import sleep

REGION = 'eucalyptus'
AMI_IMAGE = 'ami-2ec1afa2c36992c55'
ADD_RUNNER = os.environ['ADD_RUNNER']


def aws(*args) -> str:
	cmd = "aws --endpoint-url http://ec2.poc.aristotle.ucsb.edu:443 " \
		f"--region {REGION} --no-paginate --no-verify-ssl --output json"
	cmd = cmd.split()
	cmd += list(args)
	return check_output(cmd, env=os.environ).decode('UTF-8')

if __name__ == "__main__":
	parser = argparse.ArgumentParser()
	parser.add_argument('-n', '--key-name', type=str, help='Name of PEM key as used by AWS')
	args = parser.parse_args()

	print('Creating instance...')
	try:
		create_output = aws('ec2', 'run-instances', \
						'--key-name', args.key_name, \
						'--image-id', AMI_IMAGE, \
						'--instance-type', 't2.xlarge')
	except CalledProcessError as error:
		print(error.output)
		exit(error.returncode)

	ec2_instance = json.loads(create_output)['Instances'][0]['InstanceId']
	public_ip = json.loads(create_output)['PublicIpAddress']
	print(f'Created new instance with ID {ec2_instance} at public IP {public_ip}.')

	while True:
		print('Checking if instance is live...', end='')
		if 'running' in aws('ec2', 'describe-instances', '--instance-ids', ec2_instance):
			print('Yes!')
			break
		print('No.')
		sleep(5)

	with open(os.environ['GITHUB_OUTPUT'], 'a+') as gh_output:
		gh_output.write(f'CURR_ID={ec2_instance}')
		gh_output.write(f'PUBLIC_IP={public_ip}')

