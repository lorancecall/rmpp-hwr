#!/bin/sh
# Cross-build build/practice_test (see practice_test.cpp). SDK defaults to ../sdk.
set -e
cd "$(dirname "$0")/../.."
SDK=${SDK:-../sdk}
CXX=${CXX:-$SDK/bin/aarch64-none-linux-gnu-g++}
Q=$SDK/qt/usr/include/qt6
mkdir -p build
"$CXX" -std=c++17 -O2 -fPIC -DQT_NO_VERSION_TAGGING -DQT_NO_DEBUG -I"$Q" -I"$Q/QtCore" \
  ui/test/practice_test.cpp -o build/practice_test -L"$SDK/devlibs" -l:libQt6Core.so.6 -Wl,--allow-shlib-undefined
ls -la build/practice_test
