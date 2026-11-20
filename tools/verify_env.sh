#!/usr/bin/env bash
#
# Prints which verification lanes this machine can run.
#
# Required for the host test suite: c++ and cmake.
# Optional lanes: Docker (vsomeip interop) and cross toolchains (ARM).

set -u

missing_required=0

check_required() {
    local label="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        printf '%-42s %s\n' "$label" "OK"
    else
        printf '%-42s %s\n' "$label" "MISSING (required)"
        missing_required=1
    fi
}

check_optional() {
    local label="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        printf '%-42s %s\n' "$label" "OK"
    else
        printf '%-42s %s\n' "$label" "absent (optional lane)"
    fi
}

echo "host"
printf '%-42s %s\n' "uname" "$(uname -srm)"
check_required "c++ compiler" c++ --version
check_required "cmake" cmake --version
check_required "python3" python3 --version

echo
echo "network"
if ip link show lo 2>/dev/null | grep -q MULTICAST; then
    printf '%-42s %s\n' "loopback multicast" "OK"
else
    printf '%-42s %s\n' "loopback multicast" "off (sudo ip link set lo multicast on)"
fi
mcast_iface=$(ip -o link show 2>/dev/null | awk '$0 ~ /MULTICAST/ && $2 != "lo:" {print $2}' |
    tr -d ':' | while read -r dev; do
        addr=$(ip -o -4 addr show dev "$dev" 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)
        if [ -n "$addr" ]; then
            printf '%s %s\n' "$dev" "$addr"
            break
        fi
    done)
if [ -n "$mcast_iface" ]; then
    printf '%-42s %s\n' "multicast test interface" "${mcast_iface#* } (${mcast_iface%% *})"
else
    printf '%-42s %s\n' "multicast test interface" "none (set SOMEIP_TEST_MCAST_IFACE yourself)"
fi

echo
echo "vsomeip interop lane"
check_optional "docker CLI" docker --version
if docker info >/dev/null 2>&1; then
    printf '%-42s %s\n' "docker daemon" "OK"
elif [ -S /var/run/docker.sock ]; then
    if id -nG 2>/dev/null | grep -qw docker; then
        printf '%-42s %s\n' "docker daemon" "socket present, access denied (check daemon logs)"
    else
        printf '%-42s %s\n' "docker daemon" \
            "socket root:docker, user not in docker group"
        printf '%-42s %s\n' "  fix" "sudo usermod -aG docker \$USER, then re-login"
    fi
else
    printf '%-42s %s\n' "docker daemon" "no socket (start dockerd)"
fi
if [ -d "${VSOMEIP_PEER_DIR:-}" ]; then
    printf '%-42s %s\n' "VSOMEIP_PEER_DIR" "${VSOMEIP_PEER_DIR}"
else
    printf '%-42s %s\n' "VSOMEIP_PEER_DIR" "unset (peer image sources)"
fi

echo
echo "target lanes"
check_optional "arm-linux-gnueabihf-g++" arm-linux-gnueabihf-g++ --version
check_optional "aarch64-linux-gnu-g++" aarch64-linux-gnu-g++ --version
check_optional "qemu-aarch64" qemu-aarch64 --version
if [ -n "${AARCH64_TOOLCHAIN_ROOT:-}" ] &&
    [ -x "${AARCH64_TOOLCHAIN_ROOT}/usr/bin/aarch64-linux-gnu-g++-11" ]; then
    printf '%-42s %s\n' "AARCH64_TOOLCHAIN_ROOT" "${AARCH64_TOOLCHAIN_ROOT}"
else
    printf '%-42s %s\n' "AARCH64_TOOLCHAIN_ROOT" \
        "unset or incomplete (see docs/validation_lanes_zh.md)"
fi

echo
if [ "${missing_required}" -ne 0 ]; then
    echo "result: host lane blocked (install the missing required tools)"
    exit 1
fi
echo "result: host lane ready"
exit 0
