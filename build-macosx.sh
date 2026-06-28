#!/bin/sh

mkdir -p ./build-macosx

cmake ./src/CMakeLists.txt -B ./build-macosx -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt6

make -C ./build-macosx
