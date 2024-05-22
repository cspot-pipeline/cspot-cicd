#!/bin/bash
# Options:
#  -k   Path to PEM key used to connect to the instance
#  -n   Name of PEM key as used by AWS
#  -p   Profile to use from AWS config (can also use environment variables)

set -e

while getopts "k:n:p:" opt; do
  case "${opt}" in
    k) KEYFILE=$(readlink -f "${OPTARG}")
      ;;
    n) KEYNAME=${OPTARG}
      ;;
    p) AWS_PROFILE="--profile ${OPTARG}"
      ;;
    *) exit
      ;;
  esac
done

SCRIPT_PATH=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
AMI_IMAGE=ami-6022f04a109e86a98
REGION="eucalyptus" 

#chmod 400 "${KEYFILE}"

function euca() {
   command aws ${AWS_PROFILE:-'--endpoint-url' 'http://ec2.poc.aristotle.ucsb.edu:443'} \
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
