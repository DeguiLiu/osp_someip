/**
 * @file loopback_bench.cpp
 * @brief Loopback RPC benchmark: discovery, then N request/response round trips.
 *
 * Server and client run in one process over real UDP sockets, so the number
 * covers the full encode/send/decode/dispatch path minus the kernel's network
 * stack traversal between hosts. Results are printed as avg / p50 / p99 / max
 * latency and requests per second.
 */

#include "osp/someip/client.hpp"
#include "osp/someip/server.hpp"
#include "osp/someip/udp_link.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace {

constexpr std::uint16_t kBenchSdPort = 31700U;
constexpr std::uint16_t kBenchClientPort = 31701U;
constexpr std::uint16_t kBenchServicePort = 31702U;
constexpr std::uint32_t kIterations = 3000U;
constexpr std::uint32_t kWarmup = 100U;

struct EchoHandler final {
  osp::someip::RequestReply OnRequest(
      const osp::someip::IncomingRequest& request) noexcept {
    osp::someip::RequestReply reply;
    reply.handled = true;
    reply.payload = request.payload;
    return reply;
  }
};

struct BenchHandler final {
  std::uint32_t responses{0U};

  void OnResponse(std::uint32_t /*id*/, osp::someip::MessageView /*message*/) noexcept {
    ++responses;
  }
  void OnEvent(osp::someip::MessageView /*message*/) noexcept {}
  void OnTimeout(std::uint32_t /*id*/) noexcept {}
  void OnServiceAvailable(osp::someip::ServiceInstance /*service*/) noexcept {}
  void OnServiceGone(osp::someip::ServiceInstance /*service*/) noexcept {}
  void OnSenderRebooted(const osp::someip::Ipv4Endpoint& /*source*/) noexcept {}
  void OnSubscribeAck(std::uint16_t /*s*/, std::uint16_t /*i*/, std::uint16_t /*eg*/,
                      std::uint32_t /*ttl*/) noexcept {}
};

struct DemoSink final {
  osp::UdpSocket* sd_socket{nullptr};
  osp::UdpSocket* service_socket{nullptr};

  void operator()(osp::span<const std::uint8_t> datagram,
                  const osp::someip::Ipv4Endpoint& to) const noexcept {
    osp::UdpSocket* socket =
        (osp::someip::kSdPort == to.port) ? sd_socket : service_socket;
    const auto address = osp::SocketAddress::FromIpv4Bytes(to.address, to.port);
    if (address.has_value()) {
      (void)socket->SendTo(datagram.data(), datagram.size(), address.value());
    }
  }
};

using BenchServer = osp::someip::Server<DemoSink, 4U, 4U, 1400U, EchoHandler>;
using BenchClient =
    osp::someip::Client<osp::someip::UdpSender, BenchHandler, 4U, 4U>;

std::uint32_t NowMs() noexcept {
  static const auto start = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

bool CreateBound(std::uint16_t port, osp::UdpSocket& socket) noexcept {
  auto created = osp::UdpSocket::Create();
  if (!created.has_value()) {
    return false;
  }
  socket = std::move(created.value());
  (void)socket.SetReuseAddress(true);
  const auto address = osp::SocketAddress::FromIpv4("127.0.0.1", port);
  if (!address.has_value() || !socket.Bind(address.value()).has_value()) {
    return false;
  }
  return 0 == osp::SetFdNonBlocking(socket.Fd(), true);
}

void PumpServer(osp::UdpSocket& sd_socket, osp::UdpSocket& service_socket,
                BenchServer& server) noexcept {
  std::uint8_t buffer[1500] = {};
  osp::SocketAddress from;
  for (std::uint32_t pass = 0U; pass < 2U; ++pass) {
    osp::UdpSocket& socket = (0U == pass) ? sd_socket : service_socket;
    for (;;) {
      const auto received = socket.RecvFrom(buffer, sizeof(buffer), from);
      if (!received.has_value()) {
        break;
      }
      server.HandleDatagram(
          osp::span<const std::uint8_t>(buffer, static_cast<std::size_t>(received.value())),
          osp::someip::EndpointOf(from), NowMs());
    }
  }
}

void PumpClient(osp::UdpSocket& socket, BenchClient& client) noexcept {
  std::uint8_t buffer[1500] = {};
  osp::SocketAddress from;
  for (;;) {
    const auto received = socket.RecvFrom(buffer, sizeof(buffer), from);
    if (!received.has_value()) {
      break;
    }
    client.HandleDatagram(
        osp::span<const std::uint8_t>(buffer, static_cast<std::size_t>(received.value())),
        osp::someip::EndpointOf(from), NowMs());
  }
}

}  // namespace

int main() {
  osp::UdpSocket sd_socket;
  osp::UdpSocket service_socket;
  osp::UdpSocket client_socket;
  if (!CreateBound(kBenchSdPort, sd_socket) || !CreateBound(kBenchServicePort, service_socket) ||
      !CreateBound(kBenchClientPort, client_socket)) {
    std::printf("failed to bind benchmark sockets\n");
    return 1;
  }

  DemoSink sink{&sd_socket, &service_socket};
  BenchServer server(sink);
  const osp::someip::ServiceInstance service =
      osp::someip::ServiceInstance::New(0x1234U, 0x0001U, 1U);
  if (!server.Offer(service, {{127U, 0U, 0U, 1U}, kBenchServicePort}, 30U, 0U)) {
    std::printf("failed to offer the benchmark service\n");
    return 1;
  }

  osp::someip::UdpSender client_sink(client_socket);
  BenchHandler handler;
  BenchClient client(client_sink, handler,
                     {{127U, 0U, 0U, 1U}, kBenchClientPort},
                     {{127U, 0U, 0U, 1U}, kBenchSdPort});
  (void)client.StartFind(0x1234U, 0U);

  for (std::uint32_t attempt = 0U; attempt < 1000U; ++attempt) {
    PumpServer(sd_socket, service_socket, server);
    PumpClient(client_socket, client);
    client.Tick(NowMs());
    if (nullptr != client.FindCached(service)) {
      break;
    }
  }
  if (nullptr == client.FindCached(service)) {
    std::printf("discovery failed\n");
    return 1;
  }

  const std::uint8_t payload[8] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
  std::uint32_t latencies[kIterations] = {};
  std::uint32_t completed = 0U;
  const auto bench_start = std::chrono::steady_clock::now();

  for (std::uint32_t iteration = 0U; iteration < (kWarmup + kIterations); ++iteration) {
    const std::uint32_t before = handler.responses;
    const auto request_start = std::chrono::steady_clock::now();
    if (!client.Request(service, 0x0001U, osp::make_span(payload), 50U, NowMs())) {
      break;
    }
    while (handler.responses == before) {
      PumpServer(sd_socket, service_socket, server);
      PumpClient(client_socket, client);
      const auto waited = std::chrono::steady_clock::now() - request_start;
      if (waited > std::chrono::milliseconds(50)) {
        break;
      }
    }
    if (iteration < kWarmup || handler.responses == before) {
      continue;  // warmup round trips are not measured
    }
    const auto elapsed = std::chrono::steady_clock::now() - request_start;
    latencies[completed] = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
    ++completed;
  }

  const auto total = std::chrono::steady_clock::now() - bench_start;
  if (0U == completed) {
    std::printf("no round trip completed\n");
    return 1;
  }
  std::sort(latencies, latencies + completed);
  std::uint64_t sum = 0U;
  for (std::uint32_t index = 0U; index < completed; ++index) {
    sum += latencies[index];
  }
  const double seconds = static_cast<double>(
                             std::chrono::duration_cast<std::chrono::microseconds>(total)
                                 .count()) /
                         1000000.0;
  std::printf(
      "round_trips=%u avg=%.1fus p50=%uus p99=%uus max=%uus rate=%.0f req/s\n",
      static_cast<unsigned>(completed), static_cast<double>(sum) / completed,
      static_cast<unsigned>(latencies[completed / 2U]),
      static_cast<unsigned>(latencies[(completed * 99U) / 100U]),
      static_cast<unsigned>(latencies[completed - 1U]),
      static_cast<double>(completed) / seconds);
  return 0;
}
