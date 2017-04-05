#!/bin/bash

rebuild() {
    pushd $1
    make clean && make
    popd
}
export -f rebuild

find . -type d  -not \( -path "./.git" -prune \) -name "PerfUtils" -exec bash -c 'rebuild $0' {} \;
find . -type d  -not \( -path "./.git" -prune \) -name "CoreArbiter" -exec bash -c 'rebuild $0' {} \;
find . -type d  -not \( -path "./.git" -prune \) -name "Arachne" -exec bash -c 'rebuild $0' {} \;

make clean && make
