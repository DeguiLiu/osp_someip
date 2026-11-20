#!/usr/bin/env bash
#
# Compiles the socket-independent core headers with the RT-Thread branch of
# newosp's platform layer enabled. A stub rtthread.h supplies the few symbols
# the layer calls, so this is a type-check, not a port.
#
# Usage: tools/check_rtthread.sh <c++ compiler> <include dir> [more include dirs...]
set -eu

COMPILER=${1:?usage: check_rtthread.sh <compiler> <include dir> [more...]}
shift
ROOT=$(cd "$(dirname "$0")/.." && pwd)
STUB="$ROOT/tools/rtthread_stub"
CORE=(
    header.hpp message.hpp payload.hpp owned_payload.hpp wire.hpp error.hpp
    message_id.hpp message_type.hpp return_code.hpp service_instance.hpp endpoint.hpp
    sd/types.hpp sd/entry.hpp sd/entry_view.hpp sd/options.hpp sd/option_view.hpp
    sd/header.hpp sd/session.hpp sd_codec.hpp
    server.hpp client.hpp tp.hpp e2e.hpp e2e_profile1.hpp
)

INCLUDES=("-I$STUB" "-I$ROOT/include")
for dir in "$@"; do
    INCLUDES+=("-I$dir")
done

tu=$(mktemp --suffix=.cpp)
trap 'rm -f "$tu" "$tu.err"' EXIT
FAIL=0
for header in "${CORE[@]}"; do
    printf '#include "osp/someip/%s"\nint main() { return 0; }\n' "$header" > "$tu"
    if ! "$COMPILER" -std=c++17 -fno-exceptions -fno-rtti -DOSP_PLATFORM_RTTHREAD=1 \
        -DOSP_HAS_NETWORK=0 -Wall -Wextra -Wpedantic -fsyntax-only "${INCLUDES[@]}" \
        "$tu" 2> "$tu.err"; then
        printf 'FAIL %s\n' "$header"
        head -6 "$tu.err"
        FAIL=1
    fi
done

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
echo "all ${#CORE[@]} core headers compile with the RT-Thread branch"
