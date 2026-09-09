#!/bin/bash
# Host-side policy tests; no game installation, Windows runtime or graphics needed.
set -euo pipefail
DIR="$(cd "$(dirname "$0")/.." && pwd)"

# v66.5: the MOD_STAMP banner is a single very long string literal that is
# edited by script; an unescaped quote inside it silently terminates the
# literal and the DLL build fails only in CI (caught the hard way on the
# first v66.5 commit). Validate it here first.
PY="$(command -v python3 || command -v python)"
"$PY" - "$DIR/src/dllmain.cpp" <<'PYGUARD'
import re, sys
src = open(sys.argv[1], encoding='utf-8', errors='replace').read()
for i, line in enumerate(src.split('\n'), 1):
    if line.startswith('#define MOD_STAMP') or line.startswith('#define MOD_BUILD'):
        body = line.split(None, 2)[2].strip()
        name = line.split()[1]
        if not re.fullmatch(r'"(?:[^"\\]|\\.)*"', body, re.S):
            sys.exit("%s on line %d is not a single well-formed string literal "
                     "(unescaped quote?)" % (name, i))
print("banner check: MOD_BUILD/MOD_STAMP are well-formed string literals")
PYGUARD

mkdir -p "$DIR/dist/tests"
"${HOST_CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    "$DIR/tests/cast_ground_test.cpp" -o "$DIR/dist/tests/cast_ground_test.exe"
"$DIR/dist/tests/cast_ground_test.exe"
"${HOST_CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    "$DIR/tests/aim_policy_test.cpp" -o "$DIR/dist/tests/aim_policy_test.exe"
"$DIR/dist/tests/aim_policy_test.exe"
"${HOST_CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    "$DIR/tests/coop_cast_test.cpp" -o "$DIR/dist/tests/coop_cast_test.exe"
"$DIR/dist/tests/coop_cast_test.exe"
"${HOST_CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    "$DIR/tests/coop_trio_test.cpp" -o "$DIR/dist/tests/coop_trio_test.exe"
"$DIR/dist/tests/coop_trio_test.exe"
"${HOST_CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    "$DIR/tests/charged_cast_test.cpp" -o "$DIR/dist/tests/charged_cast_test.exe"
"$DIR/dist/tests/charged_cast_test.exe"
"${HOST_CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    "$DIR/tests/native_aim_test.cpp" -o "$DIR/dist/tests/native_aim_test.exe"
"$DIR/dist/tests/native_aim_test.exe"
"${HOST_CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    "$DIR/tests/lumos_sync_test.cpp" -o "$DIR/dist/tests/lumos_sync_test.exe"
"$DIR/dist/tests/lumos_sync_test.exe"
"${HOST_CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    "$DIR/tests/stuck_pawn_test.cpp" -o "$DIR/dist/tests/stuck_pawn_test.exe"
"$DIR/dist/tests/stuck_pawn_test.exe"
"${HOST_CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    "$DIR/tests/live_index_test.cpp" -o "$DIR/dist/tests/live_index_test.exe"
"$DIR/dist/tests/live_index_test.exe"
