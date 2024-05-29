#!/bin/bash

curr_runners=$(curl -L \
  -H "Accept: application/vnd.github+json" \
  -H "Authorization: Bearer $ADD_RUNNER" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  https://api.github.com/orgs/cspot-pipeline/cspot-cicd/actions/runners)
  
num_runners=$(echo "$curr_runners" | jq -r '.total_count')
runner_name="cspot-runner-$num_runners"
echo $runner_name

# Perform the curl call and store the JSON response in a variable
json_response=$(
    curl -L \
      -X POST \
      -H "Accept: application/vnd.github+json" \
      -H "Authorization: Bearer $ADD_RUNNER" \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      https://api.github.com/orgs/cspot-pipeline/cspot-cicd/actions/runners/generate-jitconfig \
      -d "{\"name\":\"$runner_name\",\"runner_group_id\":1, \"labels\":[\"self-hosted\",\"X64\",\"Linux\"]}")

# Print the JSON response to verify
echo "$json_response"
