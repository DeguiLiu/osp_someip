#!/usr/bin/env bash
#
# Runs one SOME/IP interop scenario against a vsomeip 3.4 peer.
#
# The script needs a locally built vsomeip and the peer program from the Rust
# prototype (tests/data/vsomeip-peer). It uses user namespaces plus a veth pair
# so each side has its own address and port space, which is what Docker's host
# networking would give; no root and no Docker are required.
#
# Environment:
#   VSOMEIP_PEER_BIN  path to the built peer binary          (required)
#   VSOMEIP_LIB_DIR   directory holding libvsomeip3*.so     (required)
#   SOMEIP_DEMO_DIR   directory holding the demo binaries   (default build/demo/examples)
#
# Usage: tools/vsomeip_interop.sh server|client [seconds]
#   server  our server, the peer acts as the client
#   client  our client, the peer offers the service
set -eu

MODE=${1:-server}
SECONDS_TO_RUN=${2:-12}
PEER_BIN=${VSOMEIP_PEER_BIN:?set VSOMEIP_PEER_BIN to the built peer}
LIB_DIR=${VSOMEIP_LIB_DIR:?set VSOMEIP_LIB_DIR to the vsomeip build directory}
DEMO_DIR=${SOMEIP_DEMO_DIR:-build/demo/examples}
OUR_IP=10.0.0.1
PEER_IP=10.0.0.2

if [ ! -x "$PEER_BIN" ]; then
    echo "peer binary not found: $PEER_BIN" >&2
    exit 1
fi
if [ ! -x "$DEMO_DIR/someip_server_demo" ] || [ ! -x "$DEMO_DIR/someip_client_demo" ]; then
    echo "demo binaries not found in $DEMO_DIR" >&2
    exit 1
fi

export MODE SECONDS_TO_RUN PEER_BIN LIB_DIR DEMO_DIR OUR_IP PEER_IP

exec unshare -rmn bash -s <<'INNER'
set -eu
ip link set lo up

unshare -n bash -c 'echo $$ > /tmp/someip-peer.pid; sleep 120' &
sleep 1
PEER_NS=$(cat /tmp/someip-peer.pid)

ip link add veth0 type veth peer name veth1
ip link set veth1 netns "$PEER_NS"
ip addr add "$OUR_IP/24" dev veth0
ip link set veth0 up
ip link set veth0 multicast on
nsenter -t "$PEER_NS" -n ip link set lo up
nsenter -t "$PEER_NS" -n ip addr add "$PEER_IP/24" dev veth1
nsenter -t "$PEER_NS" -n ip link set veth1 up
nsenter -t "$PEER_NS" -n ip link set veth1 multicast on
nsenter -t "$PEER_NS" -n ip route add 239.255.0.255/32 dev veth1
ip route add 239.255.0.255/32 dev veth0

CONFIG=$(mktemp)
python3 - "$PEER_IP" "$MODE" "$CONFIG" <<'PY'
import json, sys
ip, mode, path = sys.argv[1], sys.argv[2], sys.argv[3]
config = {
    "unicast": ip,
    "netmask": "255.0.0.0",
    "logging": {"level": "info", "console": "false"},
    "applications": [{"name": "peer", "id": "0x1277"}],
    "routing": "peer",
    "service-discovery": {
        "enable": "true",
        "multicast": "239.255.0.255",
        "port": "30490",
        "protocol": "udp",
        "initial_delay_min": "10",
        "initial_delay_max": "50",
        "repetitions_base_delay": "100",
        "repetitions_max": "2",
        "ttl": "3",
        "cyclic_offer_delay": "1000",
        "request_response_delay": "0",
    },
}
if mode == "client":
    config["services"] = [{"service": "0x1234", "instance": "0x0001", "unreliable": "30509"}]
with open(path, "w") as handle:
    json.dump(config, handle, indent=2)
PY

export LD_LIBRARY_PATH="$LIB_DIR:${LD_LIBRARY_PATH:-}"
export VSOMEIP_CONFIGURATION="$CONFIG"
export VSOMEIP_APPLICATION_NAME=peer

if [ "$MODE" = "server" ]; then
    "$DEMO_DIR/someip_server_demo" --iface "$OUR_IP" --service 1234 \
        --seconds "$SECONDS_TO_RUN" &
    sleep 2
    {
        sleep 2; echo "require 1234 0001 1"
        sleep 3; echo "subscribe 1234 0001 0001 1 8001 -"
        sleep 3; echo "call 1234 0001 0001 deadbeef"
        sleep 4; echo quit
    } | nsenter -t "$PEER_NS" -n "$PEER_BIN"
    wait
else
    {
        echo "offer 1234 0001 1 0001 8001 - 0001"
        sleep 8; echo "notify 1234 0001 8001 c0ffee"
        sleep 4; echo quit
    } | nsenter -t "$PEER_NS" -n "$PEER_BIN" &
    PEER_JOB=$!
    sleep 3
    "$DEMO_DIR/someip_client_demo" --server "$PEER_IP" --local "$OUR_IP" \
        --port 30510 --service 1234 --method 0001 --multicast \
        --seconds "$SECONDS_TO_RUN"
    wait "$PEER_JOB"
fi
INNER
