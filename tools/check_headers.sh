#!/usr/bin/env bash
#
# Compiles every public header on its own. A header that relies on another
# header being included first fails here, which keeps the include graph honest.
#
# Usage: tools/check_headers.sh <c++ compiler> <include dir> [more include dirs...]
set -eu

COMPILER=${1:?usage: check_headers.sh <compiler> <include dir> [more...]}
shift
INCLUDES=()
for dir in "$@"; do
    INCLUDES+=("-I$dir")
done

HEADER_ROOT=$(cd "$(dirname "$0")/.." && pwd)/include
LIST=$(mktemp)
find "$HEADER_ROOT/osp/someip" -name '*.hpp' | sort > "$LIST"
COUNT=$(wc -l < "$LIST")
FAIL=0

while read -r header; do
    tu=$(mktemp --suffix=.cpp)
    printf '#include "%s"\nint main() { return 0; }\n' "${header#"$HEADER_ROOT"/}" > "$tu"
    if ! "$COMPILER" -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic \
        -fsyntax-only "${INCLUDES[@]}" "$tu" 2> "$tu.err"; then
        printf 'FAIL %s\n' "${header#"$HEADER_ROOT"/}"
        head -5 "$tu.err"
        FAIL=1
    fi
    rm -f "$tu" "$tu.err"
done < "$LIST"

rm -f "$LIST"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
echo "all $COUNT headers compile on their own"
