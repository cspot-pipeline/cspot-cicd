#!/bin/bash

token_json=$(curl -L \
  -X POST \
  -H "Accept: application/vnd.github+json" \
  -H "Authorization: Bearer $ADD_RUNNER" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  https://api.github.com/repos/cspot-pipeline/cspot-cicd/actions/runners/registration-token)

token=$(echo "$token_json" | jq -r '.token')
echo "Token: $token"

curr_runners=$(curl -L \
  -H "Accept: application/vnd.github+json" \
  -H "Authorization: Bearer $ADD_RUNNER" \
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
      -H "Authorization: Bearer $ADD_RUNNER" \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      https://api.github.com/repos/cspot-pipeline/cspot-cicd/actions/runners/generate-jitconfig \
      -d "{\"name\":\"$runner_name\",\"runner_group_id\":1, \"labels\":[\"self-hosted\",\"X64\",\"Linux\"]}")

# Print the JSON response to verify
echo "$json_response"

# Create a folder
mkdir actions-runner && cd actions-runner

# Download the latest runner package
curl -o actions-runner-linux-x64-2.316.1.tar.gz -L https://github.com/actions/runner/releases/download/v2.316.1/actions-runner-linux-x64-2.316.1.tar.gz

# Optional: Validate the hash
echo "d62de2400eeeacd195db91e2ff011bfb646cd5d85545e81d8f78c436183e09a8  actions-runner-linux-x64-2.316.1.tar.gz" | shasum -a 256 -c

# Extract the installer
tar xzf ./actions-runner-linux-x64-2.316.1.tar.gz

# Create the runner and start the configuration experience
./config.sh --url https://github.com/cspot-pipeline/cspot-cicd --token $token
# Last step, run it!
./run.sh

