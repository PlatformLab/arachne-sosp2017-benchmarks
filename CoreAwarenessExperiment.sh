#!/bin/bash

# Vary the number of cores with a fixed number of kernel threads (the correct
# number 5, due to one dispatch and 4 workers)
# For both, we have report one less becaues the dispatch thread takes one.

CORE_RANGE="4"
KERNEL_THREAD_RANGE="1 2 3 4 5 6 7 8"

echo Cores,Kernel Threads,Duration,Offered Load,Core Utilization,Median Latency,99\% Latency,Throughput,Load Factor,Core++,Core--
for i in $CORE_RANGE; do
  for j in $KERNEL_THREAD_RANGE; do
    echo -n $i,$j,
    ./VaryCoreAndKThread.sh $i $j | tail -n 1
    sleep 1
  done
done
