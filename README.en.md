# osp-someip

English | [中文](README.md)

A SOME/IP stack for embedded Linux and RT-Thread, written as a C++17 header-only library on top of
[newosp](https://github.com/DeguiLiu/newosp). It covers the wire format, Service Discovery, the
server side and the client side. Sockets and threads are not part of the library: the caller
supplies send, receive and time, so the core runs inside an existing event loop or RTOS task.

## Why this library

- It fits targets without a heap: the whole path is allocation-free (`tests/no_alloc_witness.cpp` is
  an executable witness) and every capacity is a template parameter fixed at compile time. A default
  server instance is 2.1 KB, a client 2.7 KB.
- It carries no runtime dependency: no sockets and no threads, the caller injects send, receive and
  time. The send path and the request handler are template parameters, so there is no vtable and no
  type erasure, and it builds with `-fno-exceptions -fno-rtti`, which keeps static review tractable.
- The protocol surface is enough for a real integration: the full SD flow including TTL expiry and
  replay, synchronous RPC dispatch, event subscribe and publish, out-of-order SOME/IP-TP
  reassembly and five E2E profiles, itemized under "Features" below.
- Every byte can be cross-checked: hand-written goldens, mutation and fuzz sweeps over SD, TP and
  E2E, a second implementation using only the Python standard library (`tools/sd_probe.py`),
  bidirectional interop with vsomeip 3.4.10, and E2E CRCs compared byte for byte against the
  AUTOSAR C reference.

Not a fit when you need payload type serialization (this library moves bytes, the caller owns type
codecs), TCP or IPv6 (see `docs/comparison_with_vsomeip_zh.md` for why UDP only), or a ready-made
daemon with a configuration system, which is what vsomeip is for.

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
  The default `NoE2E` costs nothing; Profiles 1 (CRC-8 SAE J1850), 2 (CRC-8/H2F with a counter
  state machine), 4 (CRC-32/AUTOSAR), 5 (CRC-16/CCITT-FALSE) and 7 (CRC-64/XZ) are built in.
- Memory: capacities are template parameters, parse results are views over caller-owned buffers, and
  the whole path avoids heap allocation; TP reassembly adds its own buffer.
- Builds with `-fno-exceptions -fno-rtti`.

## Quick start

```bash
cmake -S . -B build -DOSP_GITHUB_MIRROR=https://ghfast.top/
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Presets are available too (`host-debug`, `host-release`, `noexc`, `asan`, `demo`):
`cmake --preset host-debug && cmake --build --preset host-debug -j8 && ctest --preset host-debug`.

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

Runnable examples are included. The server offers, echoes requests and publishes an event once per
second; the client discovers, subscribes and sends one RPC:

```bash
cmake -S . -B build-demo -DSOMEIP_BUILD_EXAMPLES=ON && cmake --build build-demo -j8
./build-demo/examples/someip_server_demo --iface 127.0.0.1 --service 1234 --seconds 6 &
./build-demo/examples/someip_client_demo --server 127.0.0.1 --local 127.0.0.1 \
    --port 30510 --service 1234 --method 0001 --seconds 4
```

The same sources also build `someip_e2e{1,2,4,5,7}_{server,client}_demo` (one pair per profile) and
`someip_fragmented_demo`, which needs no sockets and reassembles 19 TP segments behind an E2E
header. Every flag is documented in the header comments of `examples/server_demo.cpp` and
`examples/client_demo.cpp`.

`tools/sd_probe.py` is an independent implementation that uses only the Python standard library. It
can act as either side, which makes it useful for checking byte-level compatibility:

```bash
python3 tools/sd_probe.py --server 127.0.0.1 --service 1234 --method 0001  # as a client
python3 tools/sd_probe.py --mode server --service 1234 --seconds 6         # as a server
```

Combination flags (`--e2e p1|p2|p4|p5|p7`, `--tp-chunk`, `--multicast`) are listed in
`docs/validation_lanes_zh.md`.

## Build options

| Option | Default | Meaning |
|---|---|---|
| `OSP_INCLUDE_DIR` | `../newosp/include` | newosp header directory |
| `SOMEIP_FETCH_NEWOSP` | OFF | Download newosp from GitHub when `OSP_INCLUDE_DIR` is missing; pinned to v0.9.6 |
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
include/osp/someip/  30 headers: wire format (header / message / wire / payload), sd/ and sd_codec,
                     server, client, tp, e2e and e2e_profile{1,2,4,5,7}, udp_link
examples/            server, client, per-profile variants, TP pipeline, loopback benchmark
tests/               Catch2 tests with hand-written goldens and SD / TP / E2E fuzz sweeps
tools/               Python probe, vsomeip interop, qemu and portability check scripts
docs/                design, API, validation, benchmark and static-analysis notes
```

`docs/api_zh.md` lists what each header is for.

## Status and limits

- ARM: cross-built with an aarch64 toolchain and exercised under both qemu-user and a full Linux
  kernel booted with qemu-system; no target-board data yet.
- Multicast: join and receive verified on a real NIC (`ens192`), not against an external node across
  a switch.
- vsomeip 3.4.10 interop passes in both directions. The two stacks run in separate network
  namespaces joined by a veth pair, so Docker is not needed; see `tools/vsomeip_interop.sh`.
- RT-Thread: compile-time portability plus a qemu self-check, nothing more yet. The 24 core headers
  compile against a stub `rtthread.h`, the lwIP backend type-checks against lwIP 2.2.0 headers, and
  15 runtime self-checks pass on qemu-vexpress-a9 (`tools/rtthread_qemu.sh`). Real boards are
  unverified, and lwIP traffic does not run yet: RT-Thread 5.4 `netdev.h` writes `#if NETDEV_IPV6`
  while Kconfig emits an empty macro. The full record is in `docs/validation_lanes_zh.md` 5.2.
- Transport is UDP only and the endpoint type is IPv4 only: the SD IPv6 endpoint and IPv6 multicast
  options encode and parse, but there is no IPv6 send or receive path. TCP is not implemented, and
  SD endpoint options that advertise TCP are filtered out instead of being used as a destination.
- SOME/IP-TP tracks 16-byte units, so segments may arrive in any order; overlapping segments are
  dropped. E2E Profiles 3, 6, 11, 22 and 44 are not implemented; the cyclic use of Profile 2 is
  described in `docs/api_zh.md`.

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
| [docs/comparison_with_vsomeip_zh.md](docs/comparison_with_vsomeip_zh.md) | Comparison with vsomeip, why UDP only, interop boundary |
| [docs/reference_projects_zh.md](docs/reference_projects_zh.md) | Reference implementations and interop peers |
| [docs/blog_osp_someip_zh.md](docs/blog_osp_someip_zh.md) | Technical blog: design trade-offs, why UDP only, interop boundary with vsomeip (Chinese) |

Environment check: `./tools/verify_env.sh`.

## License

MIT, see [LICENSE](LICENSE).
