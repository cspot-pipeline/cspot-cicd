#!/bin/bash

# Options:
#  -t   PAT token

set -e

while getopts "t:" opt; do
  case "${opt}" in
    t) CONFIG_RUNNER=${OPTARG}
      ;;
    *) exit
      ;;
  esac
done

token_json=$(curl -L \
  -X POST \
  -H "Accept: application/vnd.github+json" \
  -H "Authorization: Bearer $CONFIG_RUNNER" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  https://api.github.com/repos/cspot-pipeline/cspot-cicd/actions/runners/registration-token)

token=$(echo "$token_json" | jq -r '.token')
echo "TOKEN=$token" | tee $GITHUB_OUTPUT

curr_runners=$(curl -L \
  -H "Accept: application/vnd.github+json" \
  -H "Authorization: Bearer $CONFIG_RUNNER" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  https://api.github.com/repos/cspot-pipeline/cspot-cicd/actions/runners)
  
num_runners=$(echo "$curr_runners" | jq -r '.total_count')
runner_name="cspot-runner-$num_runners"
echo "Runner name: $runner_name"

# Perform the curl call and store the JSON response in a variable
json_response=$(
    curl -L \
      -X POST \
      -H "Accept: application/vnd.github+json" \
      -H "Authorization: Bearer $CONFIG_RUNNER" \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      https://api.github.com/repos/cspot-pipeline/cspot-cicd/actions/runners/generate-jitconfig \
      -d "{\"name\":\"$runner_name\",\"runner_group_id\":1, \"labels\":[\"self-hosted\",\"X64\",\"Linux\"]}")

# Print the JSON response to verify
echo "$json_response"

