#!/bin/bash
# Options:
#  -i instance id

set -e

while getopts "i:" opt; do
  case "${opt}" in
    i) INSTANCE_ID=${OPTARG}
      ;;
    *) exit
      ;;
  esac
done

SCRIPT_PATH=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
REGION="eucalyptus"

#chmod 400 "${KEYFILE}"

function euca() {
   command aws ${AWS_PROFILE:-'--endpoint-url' 'http://ec2.poc.aristotle.ucsb.edu:443'} \
      --region "${REGION}" --no-paginate --no-verify-ssl --output text "$@"
}

echo 'Terminating instance...'
EC2_INSTANCE="$(euca ec2 terminate-instances \
                    --instance-ids "${INSTANCE_ID}" \
                | grep INSTANCES \
                | awk -F' ' '{ print $6; }')"

echo "Terminated instance with ID ${INSTANCE_ID}."
