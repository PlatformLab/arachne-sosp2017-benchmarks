#!/bin/bash

# Vary the number of cores with a fixed number of kernel threads (the correct
# number 5, due to one dispatch and 4 workers)
# For both, we have report one less becaues the dispatch thread takes one.

CORE_RANGE="2 3 4 5 6 7"
KERNEL_THREAD_RANGE="2 3 4 5 6 7"

echo Cores,Kernel Threads,Duration,Offered Load,Core Utilization,Median Latency,99% Latency,Throughput
for i in $CORE_RANGE; do
  for j in 2 3 4 5 6 7; do
    echo -n $((i - 1)),$((j - 1)),
    ./VaryCoreAndKThread.sh $i $j | tail -n 1
    sleep 1
  done
done
