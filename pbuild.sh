#!/bin/bash
cmake -G Ninja -B build -DCMAKE_CXX_COMPILER=clang++ "$@"
