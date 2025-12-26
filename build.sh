#!/bin/bash
mkdir -p ./build
pushd ./build
c++ ../code/main.cpp ../code/jpeg.cpp ../code/common.cpp  -o main.out `sdl2-config --cflags --libs`
popd

