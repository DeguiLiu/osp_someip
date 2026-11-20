#!/usr/bin/env python3
"""Independent SOME/IP-SD probe: find, subscribe, request, listen.

This is a second implementation of the wire format on purpose: running it
against examples/someip_server_demo proves the C++ encoder and decoder agree
with bytes produced by unrelated code, without needing vsomeip or Docker.

Usage:
  tools/sd_probe.py --server 127.0.0.1 --service 1234 --method 0001
"""

import argparse
import select
import socket
import struct
import time

SD_PORT = 30490


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("client", "server"), default="client")
    parser.add_argument("--server", default="127.0.0.1")
    parser.add_argument("--local", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=30510)
    parser.add_argument("--service", type=lambda v: int(v, 16), default=0x1234)
    parser.add_argument("--instance", type=lambda v: int(v, 16), default=0x0001)
    parser.add_argument("--major", type=int, default=1)
    parser.add_argument("--eventgroup", type=lambda v: int(v, 16), default=0x0001)
    parser.add_argument("--method", type=lambda v: int(v, 16), default=0x0001)
    parser.add_argument("--ttl", type=int, default=10)
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--skip-request", action="store_true")
    parser.add_argument(
        "--e2e",
        choices=("none", "p1", "p4", "p5", "p7"),
        default="none",
        help="protect requests and replies with E2E Profile 1/4/5/7 (data ID 0x1234)",
    )
    parser.add_argument(
        "--tp-chunk",
        type=int,
        default=0,
        help="split the request into TP segments of this many bytes (multiple of 16)",
    )
    return parser.parse_args()


def someip_header(message_id, length, request_id, message_type=0x00, return_code=0x00):
    return struct.pack(
        ">IIIBBBB", message_id, length, request_id, 0x01, 0x01, message_type, return_code
    )


def ipv4_option(ip, port, protocol=0x11):
    payload = socket.inet_aton(ip) + b"\x00" + bytes([protocol]) + struct.pack(">H", port)
    # The length field counts the discard byte and the payload, not itself
    # nor the type byte.
    return struct.pack(">HBB", len(payload) + 1, 0x04, 0x00) + payload


def sd_message(entries, options, request_id):
    body = (
        struct.pack(">BBBB", 0x40, 0, 0, 0)
        + struct.pack(">I", 16 * len(entries))
        + b"".join(entries)
        + struct.pack(">I", len(options))
        + options
    )
    return someip_header(0xFFFF8100, 8 + len(body), request_id, 0x02, 0x00) + body


def find_datagram(service_id, request_id):
    entry = struct.pack(
        ">BBBBHHB3sI",
        0x00,
        0,
        0,
        0x10,
        service_id,
        0xFFFF,
        0xFF,
        b"\xFF\xFF\xFF",
        0xFFFFFFFF,
    )
    return sd_message([entry], b"", request_id)


def subscribe_datagram(args, request_id):
    entry = struct.pack(
        ">BBBBHHB3sHH",
        0x06,
        0,
        0,
        0x10,
        args.service,
        args.instance,
        args.major,
        args.ttl.to_bytes(3, "big"),
        0,
        args.eventgroup,
    )
    return sd_message([entry], ipv4_option(args.local, args.port), request_id)


E2E_DATA_ID = bytes([0x12, 0x34])
E2E_COUNTER = [0]
E2E_INSTANCE_WORD = 0x01001234  # instance 1 in the high byte, data ID 0x1234


def crc8_sae_j1850(data):
    crc = 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1D) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc ^ 0xFF


def crc32_autosar(data):
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xC8DF352F) if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def crc16_ccitt_false(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def crc64_xz(data):
    crc = 0xFFFFFFFFFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xC96C5795D7870F42) if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFFFFFFFFFF


def e2e_protect(payload, profile="p1"):
    """Returns the protected body for the requested profile."""
    if profile == "p1":
        counter = E2E_COUNTER[0] & 0x0F
        E2E_COUNTER[0] = (E2E_COUNTER[0] + 1) & 0x0F
        crc = crc8_sae_j1850(E2E_DATA_ID + bytes([counter, 0]) + payload)
        return bytes([counter, crc]) + payload
    if profile == "p4":
        counter = E2E_COUNTER[0] & 0xFFFF
        E2E_COUNTER[0] = (E2E_COUNTER[0] + 1) & 0xFFFF
        head = struct.pack(">HHI", 12 + len(payload), counter, E2E_INSTANCE_WORD)
        crc = crc32_autosar(head + payload)
        return head[:8] + struct.pack(">I", crc) + payload
    if profile == "p5":
        counter = E2E_COUNTER[0] & 0xFF
        E2E_COUNTER[0] = (E2E_COUNTER[0] + 1) & 0xFF
        crc = crc16_ccitt_false(bytes([counter]) + payload + E2E_DATA_ID[::-1])
        return struct.pack(">HB", crc, counter) + payload
    counter = E2E_COUNTER[0] & 0xFFFFFFFF
    E2E_COUNTER[0] = (E2E_COUNTER[0] + 1) & 0xFFFFFFFF
    head = struct.pack(">III", 20 + len(payload), counter, 0x1234)
    crc = crc64_xz(head + payload)
    return struct.pack(">Q", crc) + head + payload


def e2e_check(body, profile="p1"):
    """Returns (ok, payload, counter) for a protected body."""
    if profile == "p1":
        if len(body) < 2:
            return False, b"", 0
        counter = body[0] & 0x0F
        expected = crc8_sae_j1850(E2E_DATA_ID + bytes([counter, 0]) + body[2:])
        return expected == body[1], body[2:], counter
    if profile == "p4":
        if len(body) < 12:
            return False, b"", 0
        counter = struct.unpack(">H", body[2:4])[0]
        expected = crc32_autosar(body[:8] + body[12:])
        return expected == struct.unpack(">I", body[8:12])[0], body[12:], counter
    if profile == "p5":
        if len(body) < 3:
            return False, b"", 0
        counter = body[2]
        expected = crc16_ccitt_false(body[2:] + E2E_DATA_ID[::-1])
        return expected == struct.unpack(">H", body[:2])[0], body[3:], counter
    if len(body) < 20:
        return False, b"", 0
    counter = struct.unpack(">I", body[12:16])[0]
    expected = crc64_xz(body[8:])
    return expected == struct.unpack(">Q", body[:8])[0], body[20:], counter


def tp_segments(args, payload, request_id, message_type=0x20):
    """Split payload into SOME/IP-TP segments; chunk must be a multiple of 16."""
    chunk = max(16, (args.tp_chunk // 16) * 16)
    segments = []
    for offset in range(0, len(payload), chunk):
        piece = payload[offset : offset + chunk]
        more = offset + len(piece) < len(payload)
        word = ((offset // 16) << 4) | (1 if more else 0)
        body = struct.pack(">I", word) + piece
        header = someip_header(
            (args.service << 16) | args.method, 8 + len(body), request_id, message_type
        )
        segments.append(header + body)
    return segments


def parse_sd(datagram):
    """Return (flags, entries, options) for one SD datagram."""
    if len(datagram) < 28:
        raise ValueError("short SD datagram")
    flags = datagram[16]
    entries_size = struct.unpack(">I", datagram[20:24])[0]
    entries = [
        datagram[24 + offset : 24 + offset + 16] for offset in range(0, entries_size, 16)
    ]
    options_at = 24 + entries_size
    options_size = struct.unpack(">I", datagram[options_at : options_at + 4])[0]
    return flags, entries, datagram[options_at + 4 : options_at + 4 + options_size]


def parse_options(options):
    parsed = []
    offset = 0
    while offset + 4 <= len(options):
        length = struct.unpack(">H", options[offset : offset + 2])[0]
        wire = length + 3
        option_type = options[offset + 2]
        if option_type == 0x04:
            address = socket.inet_ntoa(options[offset + 4 : offset + 8])
            port = struct.unpack(">H", options[offset + 10 : offset + 12])[0]
            parsed.append(("ipv4", address, port))
        offset += wire
    return parsed


def run_client(args):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", args.port))
    sock.settimeout(0.5)
    server = (args.server, SD_PORT)

    sock.sendto(find_datagram(args.service, 1), server)
    print("find sent")

    service_endpoint = None
    deadline = time.monotonic() + args.seconds
    subscribed = False
    request_sent = False
    request_id = 0x00010001

    while time.monotonic() < deadline:
        try:
            datagram, source = sock.recvfrom(2048)
        except socket.timeout:
            continue
        if len(datagram) < 16:
            continue
        message_id = struct.unpack(">I", datagram[0:4])[0]
        message_type = datagram[14]

        if message_id == 0xFFFF8100:
            flags, entries, options = parse_sd(datagram)
            for entry in entries:
                entry_type = entry[0]
                service = struct.unpack(">H", entry[4:6])[0]
                instance = struct.unpack(">H", entry[6:8])[0]
                if entry_type == 0x01 and service == args.service:
                    endpoints = parse_options(options)
                    service_endpoint = endpoints[0][1:] if endpoints else source
                    print(
                        f"offer service=0x{service:04X} instance=0x{instance:04X} "
                        f"endpoint={service_endpoint} flags=0x{flags:02X}"
                    )
                    if not subscribed:
                        sock.sendto(subscribe_datagram(args, 2), source)
                        subscribed = True
                        print("subscribe sent")
                elif entry_type == 0x07:
                    granted = int.from_bytes(entry[9:12], "big")
                    print(f"subscribe ack granted ttl={granted}")
                    if granted and not args.skip_request and not request_sent:
                        if args.tp_chunk:
                            body = bytes(range(128))
                            for segment in tp_segments(args, body, request_id):
                                sock.sendto(
                                    segment, (service_endpoint[0], service_endpoint[1])
                                )
                            print(f"request sent as tp segments of {args.tp_chunk}")
                        else:
                            body = struct.pack(">I", 0x11223344)
                            if args.e2e != "none":
                                body = e2e_protect(body, args.e2e)
                            header = someip_header(
                                (args.service << 16) | args.method,
                                8 + len(body),
                                request_id,
                            )
                            sock.sendto(
                                header + body, (service_endpoint[0], service_endpoint[1])
                            )
                            print("request sent")
                        request_sent = True
        elif message_type == 0x80:
            payload = datagram[16:]
            if args.e2e != "none":
                ok, payload, counter = e2e_check(payload, args.e2e)
                print(f"e2e check={'ok' if ok else 'failed'} counter={counter}")
            print(
                f"response request_id=0x{struct.unpack('>I', datagram[8:12])[0]:08X} "
                f"payload={payload.hex()}"
            )
        elif message_type == 0x02:
            payload = datagram[16:]
            print(
                f"event method=0x{message_id & 0xFFFF:04X} payload={payload.hex()}"
            )

    print("done")


def offer_datagram(args, request_id):
    entry = struct.pack(
        ">BBBBHHB3sI",
        0x01,
        0,
        0,
        0x10,
        args.service,
        args.instance,
        args.major,
        args.ttl.to_bytes(3, "big"),
        0,
    )
    return sd_message([entry], ipv4_option("127.0.0.1", 30509), request_id)


def ack_datagram(args, request_id):
    entry = struct.pack(
        ">BBBBHHB3sHH",
        0x07,
        0,
        0,
        0x00,
        args.service,
        args.instance,
        args.major,
        args.ttl.to_bytes(3, "big"),
        0,
        args.eventgroup,
    )
    return sd_message([entry], b"", request_id)


def run_server(args):
    """Answer find/subscribe/request and publish an event every second."""
    sd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sd_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sd_sock.bind(("0.0.0.0", SD_PORT))
    service_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    service_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    service_sock.bind(("0.0.0.0", 30509))

    subscribers = set()
    session = [0]

    def next_session():
        session[0] = (session[0] % 0xFFFF) + 1
        return session[0]

    deadline = time.monotonic() + args.seconds
    last_publish = 0.0
    counter = 0
    while time.monotonic() < deadline:
        readable, _, _ = select.select([sd_sock, service_sock], [], [], 0.05)
        for sock in readable:
            datagram, source = sock.recvfrom(2048)
            if len(datagram) < 16:
                continue
            message_id, length, request_id, _, _, message_type, _ = struct.unpack(
                ">IIIBBBB", datagram[:16]
            )
            if message_id == 0xFFFF8100:
                _, entries, options = parse_sd(datagram)
                for entry in entries:
                    if entry[0] == 0x00:
                        sd_sock.sendto(offer_datagram(args, next_session()), source)
                        print(f"find from {source} -> offer sent")
                    elif entry[0] == 0x06:
                        endpoints = parse_options(options)
                        endpoint = (
                            (endpoints[0][1], endpoints[0][2]) if endpoints else source
                        )
                        subscribers.add(endpoint)
                        sd_sock.sendto(ack_datagram(args, next_session()), source)
                        print(f"subscribe from {source} -> ack to {endpoint}")
            elif message_type in (0x00, 0x20):
                body = datagram[16 : 16 + max(0, length - 8)]
                if message_type == 0x20:
                    # TP request: the probe only counts the segments.
                    print(f"tp segment of {len(body) - 4} bytes")
                    continue
                if args.tp_chunk:
                    pattern = bytes(range(128))
                    for segment in tp_segments(args, pattern, request_id, 0xA0):
                        service_sock.sendto(segment, source)
                    print(f"request 0x{message_id & 0xFFFF:04X} -> tp response of 128 bytes")
                else:
                    if args.e2e != "none":
                        ok, payload, counter = e2e_check(body, args.e2e)
                        if not ok:
                            print("e2e check failed, request dropped")
                            continue
                        print(f"e2e check=ok counter={counter}")
                        body = payload
                    if args.e2e != "none":
                        body = e2e_protect(body, args.e2e)
                    response = (
                        someip_header(message_id, 8 + len(body), request_id, 0x80, 0x00)
                        + body
                    )
                    service_sock.sendto(response, source)
                    print(f"request 0x{message_id & 0xFFFF:04X} -> {len(body)} bytes echoed")

        now = time.monotonic()
        if subscribers and (now - last_publish) >= 1.0:
            last_publish = now
            counter += 1
            payload = struct.pack(">I", counter)
            header = someip_header(
                (args.service << 16) | 0x8001, 8 + len(payload), next_session(), 0x02, 0x00
            )
            for endpoint in list(subscribers):
                service_sock.sendto(header + payload, endpoint)
            print(f"event {counter} -> {len(subscribers)} subscriber(s)")

    print("done")


def main():
    args = parse_args()
    if "server" == args.mode:
        run_server(args)
    else:
        run_client(args)


if __name__ == "__main__":
    main()
