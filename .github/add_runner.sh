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
