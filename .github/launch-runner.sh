#!/bin/bash
# Options:
#  -n   Name of PEM key as used by AWS

set -e

while getopts "n:" opt; do
  case "${opt}" in
    n) KEYNAME=${OPTARG}
      ;;
    *) exit
      ;;
  esac
done

SCRIPT_PATH=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
AMI_IMAGE=ami-2ec1afa2c36992c55
REGION="eucalyptus" 

function euca() {
   command aws --endpoint-url 'http://ec2.poc.aristotle.ucsb.edu:443' \
      --region "${REGION}" --no-paginate --no-verify-ssl --output text "$@"
}

echo 'Creating instance...'
EC2_INSTANCE="$(euca ec2 run-instances \
                    --key-name "${KEYNAME}" \
                    --image-id "${AMI_IMAGE}" \
                    --instance-type t2.xlarge \
                | grep INSTANCES \
                | awk -F' ' '{ print $6; }')"

echo "Created new instance with ID ${EC2_INSTANCE}."

function euca_describe() {
  euca ec2 describe-instances --instance-ids "${EC2_INSTANCE}"
}

while true; do
  printf 'Checking if instance is live...'
  (euca_describe | grep -q running) && break
  echo 'No.'
  sleep 5
done
echo 'Yes!'

EC2_HOSTNAME="$(euca_describe | grep -m1 ASSOCIATION | awk -F' ' '{ print $3; }')"

echo "Hostname is ${EC2_HOSTNAME}"
echo "${INSTANCE_ID}=${EC2_INSTANCE}" >> "$GITHUB_ENV"
