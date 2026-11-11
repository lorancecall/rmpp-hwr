#!/bin/sh
# Cross-build librmpp-hwr-ui.so (toolbar button, Settings > Handwriting, lasso Convert to ink). SDK defaults to ../sdk.
# DEV=1 adds the developer tools (probe, experiment loader; see ui/hwrui.cpp and ui/exp/).
set -e
cd "$(dirname "$0")/.."
SDK=${SDK:-../sdk}
CXX=${CXX:-$SDK/bin/aarch64-none-linux-gnu-g++}
Q=$SDK/qt/usr/include/qt6
mkdir -p build
DEVFLAG=; [ "${DEV:-0}" = 1 ] && DEVFLAG=-DRMPP_HWR_DEV
"$CXX" -std=c++17 -O2 -fPIC -shared -DQT_NO_VERSION_TAGGING -DQT_NO_DEBUG $DEVFLAG \
  -I"$Q" -I"$Q/QtCore" -I"$Q/QtGui" -I"$Q/QtQml" -I"$Q/QtQuick" \
  ui/hwrui.cpp -o build/librmpp-hwr-ui.so -L"$SDK/devlibs" \
  -l:libQt6Core.so.6 -l:libQt6Gui.so.6 -l:libQt6Qml.so.6 -l:libQt6Quick.so.6 \
  -ldl -Wl,--allow-shlib-undefined -Wl,-soname,librmpp-hwr-ui.so
ls -la build/librmpp-hwr-ui.so
