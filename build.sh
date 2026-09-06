#!/bin/bash
# Build the 32-bit d3d8 proxy DLL with MinGW-w64.
#   Debian/Ubuntu: sudo apt install g++-mingw-w64-i686
#   Output: dist/d3d8.dll  (copy it, with hp3mod.ini, into the game system dir)
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-i686-w64-mingw32-g++}"
mkdir -p "$DIR/dist"
echo "[*] compiling 32-bit d3d8 proxy..."
"$CXX" -O2 -s -shared -static-libgcc -static-libstdc++ \
     -o "$DIR/dist/d3d8.dll" \
     "$DIR/src/dllmain.cpp" "$DIR/src/d3d8.def" \
     -Wl,--enable-stdcall-fixup -lwinmm
echo "[*] built: $DIR/dist/d3d8.dll"
