#!/bin/bash
# Options:
#  -r   Private key (as a string)
#  -u   Public key (as a string)
#  -n   Name of PEM key as used by AWS
#  -p   Profile to use from AWS config (can also use environment variables)

set -e

while getopts "r:u:n:p:" opt; do
  case "${opt}" in
    r) PRIVATE_KEY=${OPTARG}
      ;;
    u) PUBLIC_KEY=${OPTARG}
      ;;
    n) KEYNAME=${OPTARG}
      ;;
    p) AWS_PROFILE="--profile ${OPTARG}"
      ;;
    *) exit 1
      ;;
  esac
done

# Check if PRIVATE_KEY and PUBLIC_KEY are set
if [ -z "${PRIVATE_KEY}" ] || [ -z "${PUBLIC_KEY}" ]; then
  echo "Both private and public keys must be provided."
  exit 1
fi

# Set up SSH agent and add the private key
eval "$(ssh-agent -s)"
ssh-add <(echo "${PRIVATE_KEY}")

# Save the public key to a temporary file
TEMP_PUB_KEY=$(mktemp)
echo "${PUBLIC_KEY}" > "${TEMP_PUB_KEY}"

SCRIPT_PATH=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
AMI_IMAGE=ami-6022f04a109e86a98
REGION="eucalyptus"

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
ssh-keygen -R "${EC2_HOSTNAME}"

echo "Stage 1 complete. Now SSH into the EC2 instance using ssh fedora@${EC2_HOSTNAME}" \
  " and proceed to stage 2."

# Clean up temporary files
rm -f "${TEMP_PUB_KEY}"
