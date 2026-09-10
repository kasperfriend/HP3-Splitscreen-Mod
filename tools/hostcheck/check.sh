#!/bin/bash
# Host-side SYNTAX check of the Windows DLL source (no mingw needed).
# src/dllmain.cpp needs the Win32 API; tools/hostcheck/windows.h is a
# minimal stub surface that is enough for g++ -fsyntax-only. This catches
# declaration-order errors, typos and -Wall/-Wextra warnings BEFORE the
# Windows CI build - the v66.5 check did this ad-hoc, this makes it
# repeatable. The real build is still build.sh / CI (mingw-w64-i686).
set -euo pipefail
DIR="$(cd "$(dirname "$0")/../.." && pwd)"
"${HOST_CXX:-g++}" -fsyntax-only -std=c++11 -fms-extensions -Wall -Wextra \
    -I"$(dirname "$0")" "$DIR/src/dllmain.cpp"
echo "dllmain.cpp: syntax clean (-Wall -Wextra, host stub Win32 surface)"
