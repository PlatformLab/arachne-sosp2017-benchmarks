#!/bin/bash

# Reasonably balanced at 85% utilization.
# ./SyntheticWorkload --minNumCores 5 --maxNumCores 5  FixedLoad_85P_4Core.bench

# Note that this experiment only makes sense when Arachne is compiled without the core arbiter.

if [ "$#" -ne 2 ]; then
    echo "Usage: ./CoreAwareness <NumCores> <NumKernelThreads>"
    exit
fi

# Vary cores and kernel threads
sudo env "PATH=$PATH" TakeCores.sh $(seq 1 $1 | paste -s -d, /dev/stdin) 0 $$
./CoreAwareness --minNumCores $2 --maxNumCores $2  FixedLoad_85P_4Core.bench
sudo env "PATH=$PATH" ReleaseCores.sh > /dev/null
