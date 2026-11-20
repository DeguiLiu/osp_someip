#!/usr/bin/env bash
#
# Builds an RT-Thread qemu-vexpress-a9 image with osp-someip on top and runs a
# self-check at boot. The RT-Thread tree is patched in place (idempotently):
#   - the self-check source is copied into applications/
#   - applications/SConscript gains both include paths
#   - rtconfig.py compiles C++ as C++17 without exceptions or RTTI
#
# Usage: tools/rtthread_qemu.sh <rt-thread-root> [board]
#   board defaults to bsp/qemu-vexpress-a9
set -eu

RTT_ROOT=${1:?usage: rtthread_qemu.sh <rt-thread-root> [board]}
BOARD=${2:-bsp/qemu-vexpress-a9}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BSP="$RTT_ROOT/$BOARD"

if [ ! -d "$BSP" ]; then
    echo "board directory not found: $BSP" >&2
    exit 1
fi
if ! command -v arm-none-eabi-g++ >/dev/null 2>&1; then
    echo "arm-none-eabi-g++ is required" >&2
    exit 1
fi
if ! command -v qemu-system-arm >/dev/null 2>&1; then
    echo "qemu-system-arm is required" >&2
    exit 1
fi

cp "$ROOT/tools/rtthread/someip_selfcheck.cpp" "$BSP/applications/someip_selfcheck.cpp"

python3 - "$BSP" "$ROOT" <<'PY'
import sys
bsp, root = sys.argv[1], sys.argv[2]

sconscript = bsp + '/applications/SConscript'
text = open(sconscript).read()
wanted = "CPPPATH = [cwd, '%s/include', '%s/../newosp/include']" % (root, root)
if wanted not in text:
    text = text.replace('CPPPATH = [cwd]', wanted)
    open(sconscript, 'w').write(text)

rtconfig = bsp + '/rtconfig.py'
text = open(rtconfig).read()
if '-std=c++17' not in text:
    text = text.replace("CXXFLAGS= DEVICE + CFPFLAGS + ' -Wall",
                        "CXXFLAGS= DEVICE + CFPFLAGS + ' -std=c++17 -fno-exceptions -fno-rtti -Wall")
    open(rtconfig, 'w').write(text)
PY

cd "$BSP"
scons -j8 >/dev/null
[ -f sd.bin ] || dd if=/dev/zero of=sd.bin bs=1024 count=65536 2>/dev/null

OUTPUT=$(mktemp)
trap 'rm -f "$OUTPUT"' EXIT
timeout 30 qemu-system-arm -M vexpress-a9 -smp cpus=2 -kernel rtthread.bin -sd sd.bin \
    -nographic > "$OUTPUT" 2>&1 || true
LINE=$(grep -m1 'someip_rtthread_checks=' "$OUTPUT" || true)
if [ -z "$LINE" ]; then
    echo "self-check did not run; last lines:" >&2
    tail -20 "$OUTPUT" >&2
    exit 1
fi
echo "$LINE"
case "$LINE" in
    *failures=0*) exit 0 ;;
    *) exit 1 ;;
esac
