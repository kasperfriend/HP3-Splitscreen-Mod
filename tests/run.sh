#!/bin/bash
# Host-side policy tests; no game installation, Windows runtime or graphics needed.
set -euo pipefail
DIR="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$DIR/dist/tests"
"${HOST_CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    "$DIR/tests/cast_ground_test.cpp" -o "$DIR/dist/tests/cast_ground_test.exe"
"$DIR/dist/tests/cast_ground_test.exe"
