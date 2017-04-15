#!/bin/bash

#for i in $( seq 0.1 0.1 1 ); do
#    p=$(printf %.0f $( echo "$i*100" | bc ));
#    echo $p;
#    ./SyntheticWorkload LoadTracking_20K_Step.bench $i 2> data/LoadTracking_20K_$p.log > data/LoadTracking_20K_$p.csv ;
#done
#
# Scaling up and down
for i in $( seq 1.0 0.25 5.0 ); do
    p=$(printf %.0f $( echo "$i*100" | bc ));
    echo $p;
    ./SyntheticWorkload --maxNumCores 15 --arraySize 33 --scalingThreshold $i --distribution poisson  LoadTracking_20K_Monster.bench \
        2> rcmonster/Monster_Poisson_LoadFactor$p.log > rcmonster/Monster_Poisson_LoadFactor$p.csv ;
done
