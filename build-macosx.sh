#!/bin/sh

mkdir -p ./build-macosx

cmake ./src/CMakeLists.txt -B ./build-macosx

make -C ./build-macosx
