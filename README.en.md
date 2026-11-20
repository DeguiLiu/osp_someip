# osp-someip

English | [中文](README.md)

A SOME/IP stack for embedded Linux and RT-Thread, written as a C++17 header-only library on top of
[newosp](https://github.com/DeguiLiu/newosp). It covers the wire format, Service Discovery, the
server side and the client side. Sockets and threads are not part of the library: the caller
supplies send, receive and time, so the core runs inside an existing event loop or RTOS task.

Scope today is M0-M3 plus SOME/IP-TP reassembly and E2E Profile 1. Board-level numbers and
third-party interop are covered in "Status and limits".

## Features

- Wire format: 16-byte SOMF header, message encode/decode, datagram walker.
- SOME/IP-TP: segment header codec and fixed-capacity reassembly, keyed by (message ID, request ID)
  with a timeout; it costs no memory unless enabled.
- Service Discovery: entry / option / header codecs with zero-copy views; builders for Find, Offer,
  StopOffer, Subscribe and Ack; session counter.
- Server: offer table, find replies (wildcards included), subscription management, event publishing,
  TTL expiry with TTL/2 replay, remote-offer observer.
- Client: discovery cache, find retries with doubling intervals, request matching and timeouts,
  subscription renewal at half TTL, availability callbacks, peer restart detection.
- RPC: `RequestHandler` is bound at compile time; requests are dispatched synchronously and answered
  with a Response or an Error. Fire-and-forget calls are supported.
- E2E: policy template with Protect on send and Check on receive; failed checks drop the message.
  The default `NoE2E` costs nothing; Profile 1 (CRC-8 SAE J1850, 4-bit counter, data ID) is built in.
- Memory: capacities are template parameters, parse results are views over caller-owned buffers, and
  the whole path avoids heap allocation. A default server instance is about 2.1 KB, a client about
  2.7 KB; TP reassembly adds its own buffer.
- Builds with `-fno-exceptions -fno-rtti`.

## Quick start

```bash
cmake -S . -B build -DOSP_GITHUB_MIRROR=https://ghfast.top/
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Presets are available too: `host-debug`, `host-release`, `noexc`, `asan`, `demo`.

```bash
cmake --preset host-debug
cmake --build --preset host-debug -j8
ctest --preset host-debug
```

On the host, 127 test cases with 12884 assertions pass; the `noexc` and ASan + UBSan builds pass
separately. An aarch64 cross build runs 126 cases under qemu with one skip, the multicast case that
needs a real NIC. A 3000-round-trip RPC benchmark over loopback gives p50 5 us and p99 9-12 us,
about 160k requests per second on x86, and p50 11 us with p99 24 us, about 66k requests per second,
on aarch64 under qemu. See `docs/benchmark_report_zh.md`.

## Using the API

The server takes its send path as a template argument, and the request handler is bound at compile
time:

```cpp
#include "osp/someip/server.hpp"

struct UdpSink {
  void operator()(osp::span<const std::uint8_t> datagram,
                  const osp::someip::Ipv4Endpoint& to) noexcept {
    // sendto(to.address, to.port, datagram)
  }
};

struct Echo {
  osp::someip::RequestReply OnRequest(const osp::someip::IncomingRequest& request) noexcept {
    return osp::someip::RequestReply{true, false, osp::someip::ReturnCode::Ok(), request.payload};
  }
};

UdpSink sink;
osp::someip::Server<UdpSink, 8U, 16U, 1400U, Echo> server(sink);

server.Offer(service, local_endpoint, 3U, now_ms);   // start offering
server.HandleDatagram(datagram, source, now_ms);     // one inbound datagram
server.Tick(now_ms);                                 // replay, timeouts
```

The client uses the same shape: a send path and a callback at construction, with `Tick()` driving
find retries, request timeouts and subscription renewal. `HandleDatagram()` does not retain the
view it is given, so the buffer can be reused as soon as the call returns.

Runnable examples are included. The server binds SD port 30490 and a service port, joins the SD
group, echoes requests and publishes an event once per second; the client discovers, subscribes,
sends one RPC and receives events:

```bash
cmake -S . -B build-demo -DSOMEIP_BUILD_EXAMPLES=ON
cmake --build build-demo -j8
./build-demo/examples/someip_server_demo --iface 127.0.0.1 --service 1234 --seconds 6 &
./build-demo/examples/someip_client_demo --server 127.0.0.1 --local 127.0.0.1 \
    --port 30510 --service 1234 --method 0001 --seconds 4
```

`someip_fragmented_demo` needs no sockets: it protects a 300-byte request with E2E Profile 1,
splits it into 19 TP segments, feeds them to a server and verifies the protected reply.

Passing `--multicast` to the client switches SD to the multicast group: it opens a second socket
on 30490 while the service traffic stays on `--port`. Without the flag, SD and service share one
socket, which suits two processes on the same host.

`tools/sd_probe.py` is an independent implementation that uses only the Python standard library. It
can act as either side of the exchange, which makes it useful for checking byte-level compatibility:

```bash
python3 tools/sd_probe.py --server 127.0.0.1 --service 1234 --method 0001
python3 tools/sd_probe.py --mode server --service 1234 --seconds 6
```

## Build options

| Option | Default | Meaning |
|---|---|---|
| `OSP_INCLUDE_DIR` | `../newosp/include` | newosp header directory |
| `SOMEIP_FETCH_NEWOSP` | OFF | Download newosp from GitHub when no sibling checkout exists |
| `SOMEIP_BUILD_TESTS` | ON | Build the Catch2 tests |
| `SOMEIP_BUILD_EXAMPLES` | OFF | Build examples and the benchmark |
| `SOMEIP_NO_EXCEPTIONS` | OFF | Add `-fno-exceptions -fno-rtti` |
| `OSP_GITHUB_MIRROR` | empty | Mirror prefix for dependency downloads |

Install and consume:

```bash
cmake -S . -B build -DSOMEIP_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/opt/osp-someip
cmake --build build -j8 && cmake --install build
```

```cmake
find_package(osp_someip CONFIG REQUIRED)
target_link_libraries(app PRIVATE osp::osp_someip)
```

newosp headers are not installed with this package. Consumers either add them to the include path or
set `OSP_INCLUDE_DIR` before `find_package`.

## Layout

```text
include/osp/someip/
  header.hpp        SOMF header, value type and view
  message.hpp       message codec and datagram walker
  payload.hpp       payload skeleton, OpaquePayload
  owned_payload.hpp fixed-capacity payload
  wire.hpp          big-endian writer, encoder CRTP skeleton
  sd/               SD types, entries, options, header, session counter
  sd_codec.hpp      SD datagram builders
  server.hpp        service provider
  client.hpp        service consumer
  udp_link.hpp      newosp UDP socket adapters
  e2e.hpp           E2E policy hook
  e2e_profile1.hpp  E2E Profile 1
  tp.hpp            SOME/IP-TP segmentation and reassembly
examples/           server, client, loopback benchmark
tests/              Catch2 tests with hand-written wire goldens
docs/               design, validation, benchmark and static-analysis notes
```

## Status and limits

- ARM: cross-built with an aarch64 toolchain and exercised under qemu; no target-board data yet.
- Multicast: join and receive verified on a real NIC (`ens192`), not against an external node across
  a switch.
- vsomeip 3.4.10 interop passes in both directions. The two stacks run in separate network
  namespaces joined by a veth pair, so Docker is not needed; see `tools/vsomeip_interop.sh` and
  `docs/validation_lanes_zh.md`.
- SOME/IP-TP tracks 16-byte units, so segments may arrive in any order; overlapping segments are
  dropped. E2E ships Profile 1 only, not Profile 2/4/5/7.

## Documentation

The design notes are written in Chinese.

| Document | Content |
|---|---|
| [docs/api_zh.md](docs/api_zh.md) | Public API reference and minimal examples (Chinese) |
| [docs/design_zh.md](docs/design_zh.md) | Architecture, module split, trade-offs |
| [docs/validation_lanes_zh.md](docs/validation_lanes_zh.md) | Validation lanes and environment prerequisites |
| [docs/benchmark_report_zh.md](docs/benchmark_report_zh.md) | Loopback latency baseline |
| [docs/static_analysis_zh.md](docs/static_analysis_zh.md) | cpplint / clang-tidy results and deviations |
| [docs/comparison_with_rust_zh.md](docs/comparison_with_rust_zh.md) | Comparison with the Rust prototype simple_someip |
| [docs/reference_projects_zh.md](docs/reference_projects_zh.md) | Reference implementations and interop peers |

Environment check: `./tools/verify_env.sh`.

## License

MIT, see [LICENSE](LICENSE).
