#!/bin/sh
# Distort Me - build the Windows plug-in from Linux with MinGW, and test it
# under Wine with the fake Photoshop host (tests/pshost).
# Usage:  ./build-windows-cross.sh /path/to/adobe_photoshop_sdk_2026_win_v2
set -e
if [ -z "$1" ]; then
  echo "Usage: ./build-windows-cross.sh <folder of the Photoshop SDK>"
  exit 1
fi
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DPHOTOSHOP_SDK_DIR="$1"
cmake --build build-win
echo "Done: build-win/DistortMe.8bf"
