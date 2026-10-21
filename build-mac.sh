#!/bin/sh
# Distort Me - build the Photoshop plug-in on macOS (Apple Silicon + Intel).
# Needs Xcode command line tools and CMake (brew install cmake).
# Usage:  ./build-mac.sh /path/to/adobe_photoshop_sdk_2026_mac
set -e
if [ -z "$1" ]; then
  echo "Usage: ./build-mac.sh <folder of the Photoshop SDK>"
  exit 1
fi
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DPHOTOSHOP_SDK_DIR="$1"
cmake --build build-mac
ctest --test-dir build-mac
# ad-hoc signature so macOS loads the unsigned bundle on this Mac
codesign --force --deep --sign - build-mac/DistortMe.plugin
echo
echo "Done: build-mac/DistortMe.plugin"
echo "Copy it to /Applications/Adobe Photoshop 2026/Plug-ins/ and restart Photoshop."
