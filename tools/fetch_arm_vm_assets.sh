#!/usr/bin/env bash
#
# Downloads the pieces tools/arm_vm.sh needs but does not vendor:
#   - Alpine aarch64 kernel (vmlinuz-virt), unpacked from its zimg wrapper
#   - static aarch64 busybox, unpacked from its apk
#
# Usage: tools/fetch_arm_vm_assets.sh [target-dir]   (default: build/arm-vm)
#
# The images are fetched from dl-cdn.alpinelinux.org; nothing is committed.
set -eu

DEST=${1:-build/arm-vm}
ALPINE=3.20
BASE="https://dl-cdn.alpinelinux.org/alpine/v${ALPINE}"
MIRROR="https://mirrors.tuna.tsinghua.edu.cn/alpine/v${ALPINE}"

# Tries the official CDN first, then the Tsinghua mirror.
fetch() {
    local output="$1"
    local path="$2"
    if curl -sSL --retry 3 --retry-delay 2 -o "$output" "$BASE/$path"; then
        return 0
    fi
    echo "official CDN failed, trying mirror" >&2
    curl -sSL --retry 3 --retry-delay 2 -o "$output" "$MIRROR/$path"
}

mkdir -p "$DEST"
cd "$DEST"

if [ -f Image ]; then
    echo "kernel: Image already present, skipping"
else
    echo "kernel: vmlinuz-virt (${ALPINE})"
    fetch vmlinuz-virt "releases/aarch64/netboot/vmlinuz-virt"

    # The Alpine kernel is a zimg wrapper; QEMU -kernel needs the raw Image.
    # Trailing bytes after the gzip stream are expected and ignored.
    python3 - <<'PY'
import zlib

data = open('vmlinuz-virt', 'rb').read()
index = data.find(b'\x1f\x8b\x08')
if index < 0:
    raise SystemExit('no gzip payload in vmlinuz-virt')
stream = zlib.decompressobj(16 + zlib.MAX_WBITS)
image = stream.decompress(data[index:])
if not image:
    raise SystemExit('empty kernel payload')
open('Image', 'wb').write(image)
PY
    rm -f vmlinuz-virt
fi
python3 - <<'PY'
with open('Image', 'rb') as handle:
    handle.seek(0x38)
    if handle.read(4) != b'ARM\x64':
        raise SystemExit('extracted kernel is not a raw arm64 Image')
PY

echo "busybox: busybox-static (${ALPINE})"
INDEX=$(mktemp)
fetch "$INDEX" "main/aarch64/APKINDEX.tar.gz"
VERSION=$(tar xzOf "$INDEX" APKINDEX | awk '/^P:busybox-static$/{getline; sub("V:", ""); print; exit}')
rm -f "$INDEX"
if [ -z "$VERSION" ]; then
    echo "busybox-static version not found in the index" >&2
    exit 1
fi
fetch busybox-static.apk "main/aarch64/busybox-static-${VERSION}.apk"
tar xzf busybox-static.apk
rm -f busybox-static.apk
cp bin/busybox.static ./busybox.static

echo "ready: $DEST/Image and $DEST/busybox.static"
echo "run:   AARCH64_TOOLCHAIN_ROOT=<root> tools/arm_vm.sh $DEST/Image $DEST/busybox.static <tests> <bench>"
