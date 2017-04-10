#!/bin/bash

# Vary the number of cores with a fixed number of kernel threads (the correct
# number 5, due to one dispatch and 4 workers)
echo Duration,Offered Load,Core Utilization,Median Latency,99% Latency,Throughput
for i in 2 3 4 5 6 7; do 
./VaryCoreAndKThread.sh $i 5 | tail -n 1
sleep 1
done

# Vary the number of kernel threads w
echo Duration,Offered Load,Core Utilization,Median Latency,99% Latency,Throughput
for i in 2 3 4 5 6 7; do 
./VaryCoreAndKThread.sh 5 $i | tail -n 1
done
