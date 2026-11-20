/**
 * @file client_demo.cpp
 * @brief Runnable SD client: finds a service, subscribes, prints events.
 *
 * Usage:
 *   someip_client_demo --server 127.0.0.1 [--port 30510] [--service 1234]
 *                      [--seconds 10]
 *
 * The client sends FindService to <server>:30490, subscribes to event group
 * 0x0001 on the first offer, and prints every notification it receives.
 */

#include "osp/log.hpp"
#include "osp/someip/client.hpp"
#include "osp/someip/udp_link.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

struct Options final {
  std::array<std::uint8_t, 4U> server{127U, 0U, 0U, 1U};
  std::array<std::uint8_t, 4U> local{127U, 0U, 0U, 1U};
  std::uint16_t local_port{30510U};
  std::uint16_t service_id{0x1234U};
  std::uint16_t method_id{0U};
  std::uint32_t seconds{10U};
};

struct DemoHandler final {
  std::uint32_t events{0U};

  void OnResponse(std::uint32_t request_id, osp::someip::MessageView message) noexcept {
    OSP_LOG_INFO("client", "response 0x%08X, %u payload bytes",
                 static_cast<unsigned>(request_id),
                 static_cast<unsigned>(message.Body().size()));
  }

  void OnEvent(osp::someip::MessageView message) noexcept {
    ++events;
    const auto body = message.Body();
    const unsigned first = 0U == body.size() ? 0U : static_cast<unsigned>(body[0]);
    OSP_LOG_INFO("client", "event 0x%04X, %u bytes, first=%u",
                 static_cast<unsigned>(message.Head().Id().MethodId()),
                 static_cast<unsigned>(body.size()), first);
  }

  void OnTimeout(std::uint32_t request_id) noexcept {
    OSP_LOG_WARN("client", "request 0x%08X timed out", static_cast<unsigned>(request_id));
  }

  void OnServiceAvailable(osp::someip::ServiceInstance service) noexcept {
    OSP_LOG_INFO("client", "service 0x%04X/0x%04X available",
                 static_cast<unsigned>(service.ServiceId()),
                 static_cast<unsigned>(service.InstanceId()));
  }

  void OnServiceGone(osp::someip::ServiceInstance service) noexcept {
    OSP_LOG_INFO("client", "service 0x%04X/0x%04X gone",
                 static_cast<unsigned>(service.ServiceId()),
                 static_cast<unsigned>(service.InstanceId()));
  }

  void OnSenderRebooted(const osp::someip::Ipv4Endpoint& source) noexcept {
    OSP_LOG_WARN("client", "sender %u.%u.%u.%u restarted; caches dropped",
                 static_cast<unsigned>(source.address[0]),
                 static_cast<unsigned>(source.address[1]),
                 static_cast<unsigned>(source.address[2]),
                 static_cast<unsigned>(source.address[3]));
  }

  void OnSubscribeAck(std::uint16_t service_id, std::uint16_t instance_id,
                      std::uint16_t event_group_id, std::uint32_t granted_ttl) noexcept {
    OSP_LOG_INFO("client", "subscribe ack 0x%04X/0x%04X eg=0x%04X ttl=%u",
                 static_cast<unsigned>(service_id), static_cast<unsigned>(instance_id),
                 static_cast<unsigned>(event_group_id), static_cast<unsigned>(granted_ttl));
  }
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
    if (0 == std::strcmp(argv[index], "--server") && (index + 1) < argc) {
      if (!ParseInterface(argv[++index], options.server)) {
        std::printf("invalid --server value\n");
        return false;
      }
    } else if (0 == std::strcmp(argv[index], "--local") && (index + 1) < argc) {
      if (!ParseInterface(argv[++index], options.local)) {
        std::printf("invalid --local value\n");
        return false;
      }
    } else if (0 == std::strcmp(argv[index], "--port") && (index + 1) < argc) {
      options.local_port = static_cast<std::uint16_t>(std::strtoul(argv[++index], nullptr, 10));
    } else if (0 == std::strcmp(argv[index], "--service") && (index + 1) < argc) {
      options.service_id = static_cast<std::uint16_t>(std::strtoul(argv[++index], nullptr, 16));
    } else if (0 == std::strcmp(argv[index], "--method") && (index + 1) < argc) {
      options.method_id = static_cast<std::uint16_t>(std::strtoul(argv[++index], nullptr, 16));
    } else if (0 == std::strcmp(argv[index], "--seconds") && (index + 1) < argc) {
      options.seconds = static_cast<std::uint32_t>(std::strtoul(argv[++index], nullptr, 10));
    } else {
      std::printf(
          "usage: %s --server IP [--local IP] [--port N] [--service HEX] [--method HEX]"
          " [--seconds N]\n",
          argv[0]);
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!ParseOptions(argc, argv, options)) {
    return 2;
  }

  auto created = osp::UdpSocket::Create();
  if (!created.has_value()) {
    std::printf("failed to create UDP socket\n");
    return 1;
  }
  osp::UdpSocket socket = std::move(created.value());
  (void)socket.SetReuseAddress(true);
  const auto bind_address =
      osp::SocketAddress::FromIpv4("0.0.0.0", options.local_port);
  if (!bind_address.has_value() || !socket.Bind(bind_address.value()).has_value()) {
    std::printf("failed to bind local UDP port %u\n",
                static_cast<unsigned>(options.local_port));
    return 1;
  }
  if (0 != osp::SetFdNonBlocking(socket.Fd(), true)) {
    std::printf("failed to switch the socket to non-blocking\n");
    return 1;
  }

  osp::someip::UdpSender sink(socket);
  DemoHandler handler;
  const osp::someip::Ipv4Endpoint local{options.local, options.local_port};
  const osp::someip::Ipv4Endpoint server{options.server, osp::someip::kSdPort};
  osp::someip::Client<osp::someip::UdpSender, DemoHandler, 4U, 4U> client(sink, handler, local,
                                                                         server);

  if (!client.StartFind(options.service_id, 0U)) {
    std::printf("failed to start the find sequence\n");
    return 1;
  }

  const auto start = std::chrono::steady_clock::now();
  bool subscribed = false;
  bool request_sent = false;
  for (;;) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    const std::uint32_t now_ms = static_cast<std::uint32_t>(elapsed);

    std::uint8_t buffer[1500] = {};
    osp::SocketAddress from;
    for (;;) {
      const auto received = socket.RecvFrom(buffer, sizeof(buffer), from);
      if (!received.has_value()) {
        break;
      }
      client.HandleDatagram(
          osp::span<const std::uint8_t>(buffer, static_cast<std::size_t>(received.value())),
          osp::someip::EndpointOf(from), now_ms);
    }
    client.Tick(now_ms);

    const auto* remote = client.FindCachedByService(options.service_id);
    if (!subscribed && nullptr != remote &&
        client.Subscribe(remote->service, 0x0001U, 10U, now_ms)) {
      subscribed = true;
    }
    if (!request_sent && 0U != options.method_id && nullptr != remote) {
      const std::uint8_t payload[4] = {0x01U, 0x02U, 0x03U, 0x04U};
      if (client.Request(remote->service, options.method_id, osp::make_span(payload), 2000U,
                         now_ms)) {
        request_sent = true;
      }
    }

    if ((now_ms / 1000U) >= options.seconds) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  OSP_LOG_INFO("client", "received %u event(s)", handler.events);
  return 0;
}
