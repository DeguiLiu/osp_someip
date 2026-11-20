/**
 * @file test_udp_loopback.cpp
 * @brief End-to-end loopback test: real UDP sockets, no Docker, no multicast.
 *
 * The test binds a server socket and a client socket on 127.0.0.1, drives
 * find/subscribe/publish through the Server, and drains both sockets with a
 * bounded non-blocking retry loop (1 ms per retry, 500 ms total).
 */

#include "osp/someip/message.hpp"
#include "osp/someip/sd/header.hpp"
#include "osp/someip/sd_codec.hpp"
#include "osp/someip/server.hpp"
#include "osp/someip/udp_link.hpp"

#include <chrono>
#include <cstdint>
#include <thread>

#include <catch2/catch_test_macros.hpp>

using osp::someip::EndpointOf;
using osp::someip::Ipv4Endpoint;
using osp::someip::MessageView;
using osp::someip::Server;
using osp::someip::ServiceInstance;
using osp::someip::SubscribeEventGroupRequest;
using osp::someip::UdpSender;
using osp::someip::sd::Entry;
using osp::someip::sd::EntryView;

namespace {

constexpr std::uint16_t kServerPort = 31500U;
constexpr std::uint16_t kClientPort = 31501U;
constexpr std::uint16_t kServicePort = 31502U;

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

}  // namespace

TEST_CASE("udp loopback - find, subscribe and publish over real sockets", "[server][udp]") {
  osp::UdpSocket server_socket;
  osp::UdpSocket client_socket;
  REQUIRE(BindLoopback(server_socket, kServerPort));
  REQUIRE(BindLoopback(client_socket, kClientPort));

  UdpSender sink(server_socket);
  Server<UdpSender, 4U, 4U> server(sink);
  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  REQUIRE(server.Offer(service, {{127U, 0U, 0U, 1U}, kServicePort}, 30U, 0U));

  const auto server_address = osp::SocketAddress::FromIpv4("127.0.0.1", kServerPort);
  REQUIRE(server_address.has_value());

  std::uint8_t out[256] = {};
  const auto find_size = osp::someip::BuildFindServiceDatagram(
      osp::make_span(out), 0x1234U, osp::someip::sd::SdSessionCounter::New().Next());
  REQUIRE(find_size.has_value());
  REQUIRE(client_socket
              .SendTo(out, find_size.value(), server_address.value())
              .has_value());

  std::uint8_t in[256] = {};
  osp::SocketAddress from;
  std::int32_t length = 0;
  REQUIRE(ReceiveFor(server_socket, in, sizeof(in), from, length));
  server.HandleDatagram(osp::span<const std::uint8_t>(in, static_cast<std::size_t>(length)),
                        EndpointOf(from), 10U);

  REQUIRE(ReceiveFor(client_socket, in, sizeof(in), from, length));
  const auto offer = MessageView::Parse(
      osp::span<const std::uint8_t>(in, static_cast<std::size_t>(length)));
  REQUIRE(offer.has_value());
  auto offer_sd = offer.value().SdHeader();
  REQUIRE(offer_sd.has_value());
  EntryView offer_entry;
  REQUIRE(offer_sd.value().NextEntry(offer_entry));
  REQUIRE(Entry::Kind::kOfferService == offer_entry.Kind());
  REQUIRE(0x1234U == offer_entry.ServiceId());

  SubscribeEventGroupRequest subscribe;
  subscribe.service_id = 0x1234U;
  subscribe.instance_id = 0x0001U;
  subscribe.major_version = 1U;
  subscribe.event_group_id = 0x0001U;
  subscribe.ttl = 10U;
  subscribe.local_ip = {127U, 0U, 0U, 1U};
  subscribe.local_rx_port = kClientPort;
  const auto subscribe_size = osp::someip::BuildSubscribeEventGroupDatagram(
      osp::make_span(out), subscribe, osp::someip::sd::SdSessionCounter::New().Next());
  REQUIRE(subscribe_size.has_value());
  REQUIRE(client_socket
              .SendTo(out, subscribe_size.value(), server_address.value())
              .has_value());

  REQUIRE(ReceiveFor(server_socket, in, sizeof(in), from, length));
  server.HandleDatagram(osp::span<const std::uint8_t>(in, static_cast<std::size_t>(length)),
                        EndpointOf(from), 20U);
  REQUIRE(1U == server.SubscriberCount());

  REQUIRE(ReceiveFor(client_socket, in, sizeof(in), from, length));
  const auto ack = MessageView::Parse(
      osp::span<const std::uint8_t>(in, static_cast<std::size_t>(length)));
  REQUIRE(ack.has_value());
  auto ack_sd = ack.value().SdHeader();
  REQUIRE(ack_sd.has_value());
  EntryView ack_entry;
  REQUIRE(ack_sd.value().NextEntry(ack_entry));
  REQUIRE(Entry::Kind::kSubscribeAck == ack_entry.Kind());
  REQUIRE(10U == ack_entry.Ttl());

  const std::uint8_t payload[2] = {0x11U, 0x22U};
  server.Publish(service, 0x0001U, 0x8001U, osp::make_span(payload), 30U);
  REQUIRE(ReceiveFor(client_socket, in, sizeof(in), from, length));
  const auto notification = MessageView::Parse(
      osp::span<const std::uint8_t>(in, static_cast<std::size_t>(length)));
  REQUIRE(notification.has_value());
  REQUIRE(0x8001U == notification.value().Head().Id().MethodId());
  REQUIRE(2U == notification.value().Body().size());
  REQUIRE(0x11U == notification.value().Body()[0]);
  REQUIRE(0x22U == notification.value().Body()[1]);
}
