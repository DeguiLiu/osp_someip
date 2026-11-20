/**
 * @file server_demo.cpp
 * @brief Runnable SD service provider: binds SD 30490 and service 30509, joins
 *        the SD group, offers a service, echoes requests and publishes a counter
 *        event every second.
 *
 * Usage:
 *   someip_server_demo [--iface 192.168.1.10] [--service 1234] [--seconds 30]
 *
 * --iface defaults to 0.0.0.0 (any interface); --seconds 0 runs forever.
 */

#include "osp/log.hpp"
#include "osp/someip/server.hpp"
#include "osp/someip/udp_link.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

/// Echo every request payload back to the caller.
struct EchoHandler final {
  osp::someip::RequestReply OnRequest(
      const osp::someip::IncomingRequest& request) noexcept {
    OSP_LOG_INFO("someip", "request 0x%04X from %u.%u.%u.%u",
                 static_cast<unsigned>(request.method_id),
                 static_cast<unsigned>(request.from.address[0]),
                 static_cast<unsigned>(request.from.address[1]),
                 static_cast<unsigned>(request.from.address[2]),
                 static_cast<unsigned>(request.from.address[3]));
    osp::someip::RequestReply reply;
    reply.handled = true;
    reply.payload = request.payload;
    return reply;
  }
};

struct Options final {
  std::array<std::uint8_t, 4U> interface_address{};
  std::uint16_t service_id{0x1234U};
  std::uint32_t seconds{0U};
};

bool ParseInterface(const char* text, std::array<std::uint8_t, 4U>& out) noexcept {
  const auto parsed = osp::SocketAddress::FromIpv4(text, 0U);
  if (!parsed.has_value()) {
    return false;
  }
  out = parsed.value().Ipv4Address();
  return true;
}

bool ParseOptions(int argc, char** argv, Options& options) noexcept {
  for (int index = 1; index < argc; ++index) {
    if (0 == std::strcmp(argv[index], "--iface") && (index + 1) < argc) {
      if (!ParseInterface(argv[++index], options.interface_address)) {
        std::printf("invalid --iface value\n");
        return false;
      }
    } else if (0 == std::strcmp(argv[index], "--service") && (index + 1) < argc) {
      options.service_id = static_cast<std::uint16_t>(std::strtoul(argv[++index], nullptr, 16));
    } else if (0 == std::strcmp(argv[index], "--seconds") && (index + 1) < argc) {
      options.seconds = static_cast<std::uint32_t>(std::strtoul(argv[++index], nullptr, 10));
    } else {
      std::printf("usage: %s [--iface IP] [--service HEX] [--seconds N]\n", argv[0]);
      return false;
    }
  }
  return true;
}

constexpr std::uint16_t kServicePort = 30509U;

bool CreateBound(std::uint16_t port, osp::UdpSocket& socket) noexcept {
  auto created = osp::UdpSocket::Create();
  if (!created.has_value()) {
    return false;
  }
  socket = std::move(created.value());
  (void)socket.SetReuseAddress(true);
  const auto address = osp::SocketAddress::FromIpv4("0.0.0.0", port);
  if (!address.has_value() || !socket.Bind(address.value()).has_value()) {
    return false;
  }
  return 0 == osp::SetFdNonBlocking(socket.Fd(), true);
}

/// Routes SD traffic (port 30490) to the SD socket and everything else to the
/// service socket, mirroring how a real node separates the two.
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

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!ParseOptions(argc, argv, options)) {
    return 2;
  }

  osp::UdpSocket sd_socket;
  osp::UdpSocket service_socket;
  if (!CreateBound(osp::someip::kSdPort, sd_socket) ||
      !CreateBound(kServicePort, service_socket)) {
    std::printf("failed to bind UDP %u/%u\n",
                static_cast<unsigned>(osp::someip::kSdPort),
                static_cast<unsigned>(kServicePort));
    return 1;
  }
  if (!osp::someip::JoinSdMulticast(sd_socket, options.interface_address)) {
    OSP_LOG_WARN("someip", "multicast join failed; only unicast SD will be received");
  }
  if (!osp::someip::SetSdMulticastInterface(sd_socket, options.interface_address)) {
    OSP_LOG_WARN("someip", "multicast interface selection failed; SD uses the default route");
  }

  DemoSink sink{&sd_socket, &service_socket};
  osp::someip::Server<DemoSink, 4U, 16U, 1400U, EchoHandler> server(sink);
  const osp::someip::ServiceInstance service =
      osp::someip::ServiceInstance::New(options.service_id, 0x0001U, 1U);
  const osp::someip::Ipv4Endpoint endpoint{options.interface_address, kServicePort};
  if (!server.Offer(service, endpoint, 30U, 0U)) {
    std::printf("failed to offer service 0x%04X\n", static_cast<unsigned>(options.service_id));
    return 1;
  }
  OSP_LOG_INFO("someip", "offering 0x%04X on port %u, SD on %u",
               static_cast<unsigned>(options.service_id),
               static_cast<unsigned>(kServicePort),
               static_cast<unsigned>(osp::someip::kSdPort));

  const auto start = std::chrono::steady_clock::now();
  std::uint32_t last_publish_ms = 0U;
  std::uint32_t counter = 0U;
  for (;;) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    const std::uint32_t now_ms = static_cast<std::uint32_t>(elapsed);

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
            osp::someip::EndpointOf(from), now_ms);
      }
    }
    server.Tick(now_ms);

    if ((now_ms - last_publish_ms) >= 1000U) {
      last_publish_ms = now_ms;
      ++counter;
      const std::uint8_t payload[4] = {
          static_cast<std::uint8_t>((counter >> 24U) & 0xFFU),
          static_cast<std::uint8_t>((counter >> 16U) & 0xFFU),
          static_cast<std::uint8_t>((counter >> 8U) & 0xFFU),
          static_cast<std::uint8_t>(counter & 0xFFU)};
      server.Publish(service, 0x0001U, 0x8001U, osp::make_span(payload), now_ms);
      OSP_LOG_INFO("someip", "counter %u -> %u subscriber(s)", counter,
                   server.SubscriberCount());
    }

    if (0U != options.seconds && (now_ms / 1000U) >= options.seconds) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return 0;
}
