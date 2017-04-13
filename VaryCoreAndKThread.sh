#!/bin/bash

# Reasonably balanced at 85% utilization.
# ./SyntheticWorkload --minNumCores 5 --maxNumCores 5  FixedLoad_85P_4Core.bench

# Note that this experiment only makes sense when Arachne is compiled without the core arbiter.

if [ "$#" -ne 2 ]; then
    echo "Usage: ./CoreAwareness <NumCores> <NumKernelThreads>"
    exit
fi

# Clean up state from previous (failed) runs
PerfUtils/scripts/ReleaseCores.sh

# Vary cores and kernel threads
./CoreAwareness --minNumCores $2 --maxNumCores $2  FixedLoad_85P_4Core.bench 1 $(seq 2 $1 | paste -s -d, /dev/stdin) 0

# Clean up cpusets
PerfUtils/scripts/ReleaseCores.sh