#!/bin/bash
# Usage: ./terminate.sh <instance-id>

set -e

INSTANCE_ID="$1"

REGION="eucalyptus"

function euca() {
   command aws --endpoint-url 'http://ec2.poc.aristotle.ucsb.edu:443' \
      --region "${REGION}" --no-paginate --no-verify-ssl --output text "$@"
}

echo 'Terminating instance...'
euca ec2 terminate-instances --instance-ids "${INSTANCE_ID}"
status=$?
if [ $status -eq 0 ]; then
  echo "Terminated instance with ID ${INSTANCE_ID}."
else
  echo "Failed to terminate instance" >&2
fi
exit $status

