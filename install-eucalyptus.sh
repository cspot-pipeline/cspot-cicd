#!/bin/bash

#NOTE: git clone the cspot repo first (eventually we will merge this in there,
#  but for now this script assumes you're running it from a directory containing
#  the root directory of a fresh cspot git repo)


# commit-dependent operations
# cd cspot
git submodule update --init --recursive
mv deps/libzmq/CMakeLists.txt deps/libzmq/CMakeLists.orig.txt
sed 's/build the tests" ON/build the tests" OFF/' deps/libzmq/CMakeLists.orig.txt > deps/libzmq/CMakeLists.txt


mkdir build
cd build/

cmake -G Ninja ..
ninja
ninja install
scl enable devtoolset-9 ./helper.sh

cp ../SELF-TEST.sh ./bin
cd ./bin
./SELF-TEST.sh