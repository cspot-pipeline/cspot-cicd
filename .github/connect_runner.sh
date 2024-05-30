#!/bin/bash

# Options:
#  -t   runner token

set -e

while getopts "t:" opt; do
  case "${opt}" in
    t) TOKEN=${OPTARG}
      ;;
    *) exit
      ;;
  esac
done

# Create a folder
mkdir actions-runner && cd actions-runner

# Download the latest runner package
curl -o actions-runner-linux-x64-2.316.1.tar.gz -L https://github.com/actions/runner/releases/download/v2.316.1/actions-runner-linux-x64-2.316.1.tar.gz

# Optional: Validate the hash
echo "d62de2400eeeacd195db91e2ff011bfb646cd5d85545e81d8f78c436183e09a8  actions-runner-linux-x64-2.316.1.tar.gz" | shasum -a 256 -c

# Extract the installer
tar xzf ./actions-runner-linux-x64-2.316.1.tar.gz

######### This next line down has to be on the runner but needs outside variables (token) so should be separated

# Create the runner and start the configuration experience
./config.sh --url https://github.com/cspot-pipeline/cspot-cicd --token $TOKEN

######### This can be on the runner in another crontab or something...

# Last step, run it!
./run.sh
