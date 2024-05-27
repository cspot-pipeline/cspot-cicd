#!/bin/bash

git clone https://github.com/cspot-pipeline/cspot-cicd.git
cd cspot-cicd || { echo "Directory cspot-cicd does not exist"; exit 1; }
cd build || { echo "Directory build does not exist"; exit 1; }
sudo ninja install
