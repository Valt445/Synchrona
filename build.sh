#!/bin/bash
set -e

mkdir -p build
cd build
cmake -DCMAKE_CXX_COMPILER=clang++ ..
make -j$(sysctl -n hw.ncpu)
./my_vulkan_engine

