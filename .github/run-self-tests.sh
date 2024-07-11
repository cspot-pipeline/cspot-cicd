#!/bin/bash

NUM_TESTS=$1

for ((i=1; i<=NUM_TESTS; i++))
do
   echo "Running self-test $i"
   # Add the command to run your self-test here
   ./self-test-command.sh
done
