#!/bin/bash

# Perform the curl call and store the JSON response in a variable
json_response=$(
    curl -L \
      -X POST \
      -H "Accept: application/vnd.github+json" \
      -H "Authorization: Bearer $ADD_RUNNER" \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      https://api.github.com/orgs/cspot-pipeline/actions/runners/generate-jitconfig \
      -d '{"name":"cspot-runner","runner_group_id":1, "labels":["self-hosted","X64","Linux"]')

# Print the JSON response to verify
echo "$json_response"
