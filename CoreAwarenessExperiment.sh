#!/bin/bash

# Vary the number of cores with a fixed number of kernel threads (the correct
# number 5, due to one dispatch and 4 workers)
# For both, we have report one less becaues the dispatch thread takes one.
echo Cores,Kernel Threads,Duration,Offered Load,Core Utilization,Median Latency,99% Latency,Throughput
for i in 2 3 4 5 6 7; do 
echo -n $((i - 1)),4,
./VaryCoreAndKThread.sh $i 5 | tail -n 1
sleep 1
done

# Vary the number of kernel threads with a fixed number of cores
echo Cores,Kernel Threads,Duration,Offered Load,Core Utilization,Median Latency,99% Latency,Throughput
for i in 2 3 4 5 6 7; do 
echo -n 4,$((i - 1)),
./VaryCoreAndKThread.sh 5 $i | tail -n 1
done
