/**
 * @file test_client_server_loopback.cpp
 * @brief Client and server in one process, talking over real loopback UDP.
 *
 * The server announces to the client socket and the client sends Find and
 * Subscribe to the server socket, so discovery, subscription and event
 * delivery all cross a real socket boundary without multicast or Docker.
 */

#include "osp/someip/client.hpp"
#include "osp/someip/message.hpp"
#include "osp/someip/sd/header.hpp"
#include "osp/someip/server.hpp"
#include "osp/someip/udp_link.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <thread>

#include <catch2/catch_test_macros.hpp>

using osp::someip::Client;
using osp::someip::EndpointOf;
using osp::someip::Ipv4Endpoint;
using osp::someip::MessageView;
using osp::someip::Server;
using osp::someip::ServiceInstance;
using osp::someip::UdpSender;

namespace {

constexpr std::uint16_t kServerPort = 31600U;
constexpr std::uint16_t kClientPort = 31601U;
constexpr std::uint16_t kServicePort = 31602U;

bool BindLoopback(osp::UdpSocket& socket, std::uint16_t port) {
  auto created = osp::UdpSocket::Create();
  if (!created.has_value()) {
    return false;
  }
  socket = std::move(created.value());
  const auto address = osp::SocketAddress::FromIpv4("127.0.0.1", port);
  if (!address.has_value() || !socket.Bind(address.value()).has_value()) {
    return false;
  }
  return 0 == osp::SetFdNonBlocking(socket.Fd(), true);
}

bool ReceiveFor(osp::UdpSocket& socket, std::uint8_t* buffer, std::size_t capacity,
                osp::SocketAddress& from, std::int32_t& length) {
  for (std::uint32_t attempt = 0U; attempt < 500U; ++attempt) {
    const auto received = socket.RecvFrom(buffer, capacity, from);
    if (received.has_value()) {
      length = received.value();
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

struct Handler final {
  std::uint32_t acks{0U};
  std::uint32_t events{0U};
  std::uint32_t granted_ttl{0U};
  std::uint8_t event_body[2]{};

  void OnResponse(std::uint32_t /*request_id*/, MessageView /*message*/) noexcept {}
  void OnEvent(MessageView message) noexcept {
    ++events;
    for (std::uint32_t i = 0U; i < message.Body().size() && i < sizeof(event_body); ++i) {
      event_body[i] = message.Body()[i];
    }
  }
  void OnTimeout(std::uint32_t /*request_id*/) noexcept {}
  void OnSubscribeAck(std::uint16_t /*service_id*/, std::uint16_t /*instance_id*/,
                      std::uint16_t /*event_group_id*/, std::uint32_t granted) noexcept {
    ++acks;
    granted_ttl = granted;
  }
  void OnServiceAvailable(osp::someip::ServiceInstance /*service*/) noexcept {}
  void OnServiceGone(osp::someip::ServiceInstance /*service*/) noexcept {}
  void OnSenderRebooted(const osp::someip::Ipv4Endpoint& /*source*/) noexcept {}
};

}  // namespace

TEST_CASE("udp loopback - client and server discovery, subscribe and publish", "[client][server][udp]") {
  osp::UdpSocket server_socket;
  osp::UdpSocket client_socket;
  REQUIRE(BindLoopback(server_socket, kServerPort));
  REQUIRE(BindLoopback(client_socket, kClientPort));

  const Ipv4Endpoint server_endpoint{{127U, 0U, 0U, 1U}, kServerPort};
  const Ipv4Endpoint client_endpoint{{127U, 0U, 0U, 1U}, kClientPort};

  UdpSender server_sink(server_socket);
  Server<UdpSender, 4U, 4U> server(server_sink, client_endpoint);
  UdpSender client_sink(client_socket);
  Handler handler;
  Client<UdpSender, Handler, 4U, 4U> client(client_sink, handler, client_endpoint,
                                            server_endpoint);

  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  REQUIRE(server.Offer(service, {{127U, 0U, 0U, 1U}, kServicePort}, 30U, 0U));

  std::uint8_t buffer[256] = {};
  osp::SocketAddress from;
  std::int32_t length = 0;

  // The announcement reaches the client and populates its discovery cache.
  REQUIRE(ReceiveFor(client_socket, buffer, sizeof(buffer), from, length));
  client.HandleDatagram(osp::span<const std::uint8_t>(buffer, static_cast<std::size_t>(length)),
                        EndpointOf(from), 10U);
  REQUIRE(1U == client.ServiceCount());
  REQUIRE(nullptr != client.FindCached(service));

  // Find goes to the server; its reply is another offer the client caches.
  REQUIRE(client.StartFind(0x1234U, 0U));
  REQUIRE(ReceiveFor(server_socket, buffer, sizeof(buffer), from, length));
  server.HandleDatagram(osp::span<const std::uint8_t>(buffer, static_cast<std::size_t>(length)),
                        EndpointOf(from), 20U);
  REQUIRE(ReceiveFor(client_socket, buffer, sizeof(buffer), from, length));
  client.HandleDatagram(osp::span<const std::uint8_t>(buffer, static_cast<std::size_t>(length)),
                        EndpointOf(from), 30U);

  // Subscribe crosses the socket boundary; the ack comes back with a TTL.
  REQUIRE(client.Subscribe(service, 0x0001U, 10U, 0U));
  REQUIRE(ReceiveFor(server_socket, buffer, sizeof(buffer), from, length));
  server.HandleDatagram(osp::span<const std::uint8_t>(buffer, static_cast<std::size_t>(length)),
                        EndpointOf(from), 40U);
  REQUIRE(1U == server.SubscriberCount());
  REQUIRE(ReceiveFor(client_socket, buffer, sizeof(buffer), from, length));
  client.HandleDatagram(osp::span<const std::uint8_t>(buffer, static_cast<std::size_t>(length)),
                        EndpointOf(from), 50U);
  REQUIRE(1U == handler.acks);
  REQUIRE(10U == handler.granted_ttl);

  // Publishing reaches the client's event endpoint.
  const std::uint8_t payload[2] = {0x99U, 0x66U};
  server.Publish(service, 0x0001U, 0x8001U, osp::make_span(payload), 60U);
  REQUIRE(ReceiveFor(client_socket, buffer, sizeof(buffer), from, length));
  client.HandleDatagram(osp::span<const std::uint8_t>(buffer, static_cast<std::size_t>(length)),
                        EndpointOf(from), 70U);
  REQUIRE(1U == handler.events);
  REQUIRE(0x99U == handler.event_body[0]);
  REQUIRE(0x66U == handler.event_body[1]);

}
