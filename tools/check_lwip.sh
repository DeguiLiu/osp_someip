#!/usr/bin/env bash
#
# Type-checks the lwIP socket backend of newosp against real lwIP headers.
# Pass the lwIP source tree (the directory holding src/include).
#
# Usage: tools/check_lwip.sh <c++ compiler> <lwip source dir> <include dir> [...]
set -eu

COMPILER=${1:?usage: check_lwip.sh <compiler> <lwip dir> <include dir> [more...]}
LWIP_DIR=${2:?usage: check_lwip.sh <compiler> <lwip dir> <include dir> [more...]}
shift 2
ROOT=$(cd "$(dirname "$0")/.." && pwd)

if [ ! -f "$LWIP_DIR/src/include/lwip/sockets.h" ]; then
    echo "not an lwIP source tree: $LWIP_DIR" >&2
    exit 1
fi

INCLUDES=("-I$ROOT/tools/lwip_port" "-I$LWIP_DIR/src/include" "-I$ROOT/include")
for dir in "$@"; do
    INCLUDES+=("-I$dir")
done

tu=$(mktemp --suffix=.cpp)
trap 'rm -f "$tu"' EXIT
printf '#include "osp/socket.hpp"\n#include "osp/someip/udp_link.hpp"\nint main() { return 0; }\n' > "$tu"

"$COMPILER" -std=c++17 -fsyntax-only -Wall -Wextra -DOSP_NET_BACKEND=1 \
    "${INCLUDES[@]}" "$tu"
echo "lwIP socket backend compiles against $LWIP_DIR"
