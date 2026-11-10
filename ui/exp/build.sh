#!/bin/sh
# Build an experiment module: ui/exp/build.sh exp1 -> build/exp1.so (load it with: echo "load /tmp/exp1.so ARG" > .../cmd)
set -e
cd "$(dirname "$0")/../.."
SDK=${SDK:-../sdk}
CXX=${CXX:-$SDK/bin/aarch64-none-linux-gnu-g++}
Q=$SDK/qt/usr/include/qt6
mkdir -p build
"$CXX" -std=c++17 -O2 -fPIC -shared -fvisibility=hidden -DQT_NO_VERSION_TAGGING -DQT_NO_DEBUG \
  -I"$Q" -I"$Q/QtCore" -I"$Q/QtGui" -I"$Q/QtQml" -I"$Q/QtQuick" \
  "ui/exp/$1.cpp" -o "build/$1.so" -L"$SDK/devlibs" \
  -l:libQt6Core.so.6 -l:libQt6Gui.so.6 -l:libQt6Qml.so.6 -l:libQt6Quick.so.6 -Wl,--allow-shlib-undefined
ls -la "build/$1.so"
