#!/bin/bash
# Host-side policy tests; no game installation, Windows runtime or graphics needed.
set -euo pipefail
DIR="$(cd "$(dirname "$0")/.." && pwd)"

# v66.5: the MOD_STAMP banner is a single very long string literal that is
# edited by script; an unescaped quote inside it silently terminates the
# literal and the DLL build fails only in CI (caught the hard way on the
# first v66.5 commit). Validate it here first - pure bash/grep, because the
# CI's msys2 runner image has no Python.
banner_guard() {
    local file="$1" line name body rest
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line%$'\r'}"
        case "$line" in
        '#define MOD_BUILD '*|'#define MOD_STAMP '*)
            read -r _ name body <<< "$line"
            case "$body" in
                '"'*'"') ;;
                *)
                    printf 'ERROR: %s is not a single string literal line\n' "$name" >&2
                    return 1
                    ;;
            esac
            rest="${body#\"}"; rest="${rest%\"}"
            if printf '%s' "$rest" | grep -qP '(?<!\\)"'; then
                printf 'ERROR: %s has an unescaped quote inside the literal\n' "$name" >&2
                return 1
            fi
            ;;
        esac
    done < "$file"
    echo "banner check: MOD_BUILD/MOD_STAMP are well-formed string literals"
}
banner_guard "$DIR/src/dllmain.cpp"

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
