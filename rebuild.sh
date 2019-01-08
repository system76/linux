#!/usr/bin/env bash

set -e

if [ ! -e .config ]
then
    make olddefconfig
    ./scripts/config --disable DEBUG_INFO
fi

time make bindeb-pkg LOCALVERSION=-system76 -j "$(nproc)"
