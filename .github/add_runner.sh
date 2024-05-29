#!/bin/bash

# Perform the curl call and store the JSON response in a variable
json_response=$(
    curl -L \
      -X POST \
      -H "Accept: application/vnd.github+json" \
      -H "Authorization: Bearer ${GH_PERSONAL_ACCESS_TOKEN}}" \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      https://api.github.com/orgs/cspot-pipeline/actions/runners/generate-jitconfig \
      -d '{"name":"cspot-runner","labels":["self-hosted","X64","Linux"]')

# Print the JSON response to verify
echo "$json_response"

'''
# Create a folder
mkdir actions-runner && cd actions-runner
# Download the latest runner package
curl -o actions-runner-osx-x64-2.316.1.tar.gz -L https://github.com/actions/runner/releases/download/v2.316.1/actions-runner-osx-x64-2.316.1.tar.gz
# Optional: Validate the hash
echo "392b9d7b6d5b4d4f3814dbf874641b894f0c72447cdf05ce93158832d2d49b6b  actions-runner-osx-x64-2.316.1.tar.gz" | shasum -a 256 -c
# Extract the installer
tar xzf ./actions-runner-osx-x64-2.316.1.tar.gz
Configure
# Create the runner and start the configuration experience
./config.sh --url https://github.com/cspot-pipeline/cspot-cicd --token AXFSEC5UUJQOA76SS6JM33TGK6GME
# Last step, run it!
./run.sh
'''
