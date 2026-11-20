#!/usr/bin/env bash
#
# Boots the aarch64 test binary and benchmark on a full Linux kernel under
# qemu-system-aarch64 (TCG). This is system-level evidence: real kernel,
# scheduler and network stack, unlike qemu-user.
#
# Prerequisites, none of which are vendored:
#   - an arm64 Linux Image (raw, "ARM\x64" magic at offset 0x38)
#   - a static aarch64 busybox
#   - the cross toolchain root used to build the binaries (for glibc)
#
# Usage:
#   AARCH64_TOOLCHAIN_ROOT=/opt/cross-aarch64/root \
#   tools/arm_vm.sh <Image> <busybox.static> <someip_tests> <someip_loopback_bench>
#
# The guest runs the tests, then the benchmark, then powers off.
set -eu

KERNEL=${1:?usage: arm_vm.sh <Image> <busybox.static> <tests> <bench>}
BUSYBOX=${2:?usage: arm_vm.sh <Image> <busybox.static> <tests> <bench>}
TESTS=${3:?usage: arm_vm.sh <Image> <busybox.static> <tests> <bench> [server_demo client_demo]}
BENCH=${4:?usage: arm_vm.sh <Image> <busybox.static> <tests> <bench> [server_demo client_demo]}
SERVER=${5:-}
CLIENT=${6:-}
ROOT=${AARCH64_TOOLCHAIN_ROOT:?set AARCH64_TOOLCHAIN_ROOT to the cross toolchain root}
LIBDIR="$ROOT/usr/aarch64-linux-gnu/lib"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/bin" "$WORK/lib" "$WORK/proc" "$WORK/sys" "$WORK/dev" "$WORK/tmp"

cp "$BUSYBOX" "$WORK/bin/busybox"
cp "$TESTS" "$WORK/bin/someip_tests"
cp "$BENCH" "$WORK/bin/someip_loopback_bench"
if [ -n "$SERVER" ] && [ -n "$CLIENT" ]; then
    cp "$SERVER" "$WORK/bin/someip_server_demo"
    cp "$CLIENT" "$WORK/bin/someip_client_demo"
fi
for lib in ld-linux-aarch64.so.1 libc.so.6 libm.so.6 libgcc_s.so.1 libstdc++.so.6; do
    cp "$LIBDIR/$lib" "$WORK/lib/"
done

cat > "$WORK/init" <<'INIT'
#!/bin/busybox sh
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
ip link set lo up
echo "KERNEL: $(uname -srm)"
/bin/someip_tests
echo "TESTS_RC=$?"
/bin/someip_loopback_bench
echo "BENCH_RC=$?"
if [ -x /bin/someip_server_demo ] && [ -x /bin/someip_client_demo ]; then
    /bin/someip_server_demo --iface 127.0.0.1 --seconds 8 &
    sleep 1
    /bin/someip_client_demo --server 127.0.0.1 --local 127.0.0.1 --port 30510 \
        --service 1234 --method 0001 --seconds 5
    echo "DEMO_RC=$?"
fi
poweroff -f
INIT
chmod +x "$WORK/init"

(cd "$WORK" && find . | cpio -o -H newc 2>/dev/null | gzip -9) > "$WORK/initramfs.cpio.gz"

exec qemu-system-aarch64 -M virt -cpu cortex-a57 -m 1024 -smp 2 -nographic -no-reboot \
    -kernel "$KERNEL" -initrd "$WORK/initramfs.cpio.gz" \
    -append "console=ttyAMA0 rdinit=/init panic=-1"
