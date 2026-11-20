/**
 * @file test_multicast.cpp
 * @brief Multicast delivery check on a real interface.
 *
 * Joining a group needs no privileges, so this runs without root. Set
 * SOMEIP_TEST_MCAST_IFACE to an IPv4 address of a multicast-capable
 * interface; without it the case is skipped. SOMEIP_TEST_MCAST_PORT
 * overrides the SD port, which keeps the test off 30490 when another
 * SOME/IP process is running.
 */

#include "osp/someip/message.hpp"
#include "osp/someip/sd/entry_view.hpp"
#include "osp/someip/sd/header.hpp"
#include "osp/someip/sd_codec.hpp"
#include "osp/someip/udp_link.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <catch2/catch_test_macros.hpp>

using osp::someip::MessageView;
using osp::someip::sd::Entry;
using osp::someip::sd::EntryView;
using osp::someip::sd::SdHeaderView;
using osp::someip::sd::SdSessionCounter;

namespace {

constexpr std::uint16_t kServiceId = 0x1234U;
constexpr std::uint16_t kDefaultPort = 31490U;

bool InterfaceFromEnv(std::array<std::uint8_t, 4U>& out) {
  const char* text = std::getenv("SOMEIP_TEST_MCAST_IFACE");
  if (nullptr == text) {
    return false;
  }
  const auto parsed = osp::SocketAddress::FromIpv4(text, 0U);
  if (!parsed.has_value()) {
    return false;
  }
  out = parsed.value().Ipv4Address();
  return true;
}

std::uint16_t PortFromEnv() {
  const char* text = std::getenv("SOMEIP_TEST_MCAST_PORT");
  if (nullptr == text) {
    return kDefaultPort;
  }
  const unsigned long value = std::strtoul(text, nullptr, 10);
  if (0UL == value || value > 65535UL) {
    return kDefaultPort;
  }
  return static_cast<std::uint16_t>(value);
}

}  // namespace

TEST_CASE("multicast - a joined socket receives an SD find", "[udp][multicast]") {
  std::array<std::uint8_t, 4U> interface_address{};
  if (!InterfaceFromEnv(interface_address)) {
    SKIP("SOMEIP_TEST_MCAST_IFACE is not set");
  }
  const std::uint16_t port = PortFromEnv();

  auto receiver_socket = osp::UdpSocket::Create();
  REQUIRE(receiver_socket.has_value());
  auto& receiver = receiver_socket.value();
  const auto any = osp::SocketAddress::FromIpv4("0.0.0.0", port);
  REQUIRE(any.has_value());
  REQUIRE(receiver.Bind(any.value()).has_value());
  REQUIRE(0 == osp::SetFdNonBlocking(receiver.Fd(), true));
  REQUIRE(osp::someip::JoinSdMulticast(receiver, interface_address));

  auto sender_socket = osp::UdpSocket::Create();
  REQUIRE(sender_socket.has_value());
  auto& sender = sender_socket.value();
  const auto local = osp::SocketAddress::FromIpv4Bytes(interface_address, 0U);
  REQUIRE(local.has_value());
  REQUIRE(sender.Bind(local.value()).has_value());
  REQUIRE(0 == osp::SetFdNonBlocking(sender.Fd(), true));
  REQUIRE(osp::someip::SetSdMulticastInterface(sender, interface_address));
  REQUIRE(osp::someip::SetSdMulticastLoop(sender, true));

  std::uint8_t datagram[64] = {};
  SdSessionCounter counter = SdSessionCounter::New();
  const auto built = osp::someip::BuildFindServiceDatagram(
      osp::make_span(datagram), kServiceId, counter.Next());
  REQUIRE(built.has_value());

  const auto group = osp::SocketAddress::FromIpv4("239.255.0.255", port);
  REQUIRE(group.has_value());
  const auto sent = sender.SendTo(datagram, built.value(), group.value());
  REQUIRE(sent.has_value());
  REQUIRE(static_cast<std::uint32_t>(sent.value()) == built.value());

  std::uint8_t received[256] = {};
  osp::SocketAddress from;
  std::int32_t length = -1;
  for (std::uint32_t attempt = 0U; attempt < 500U; ++attempt) {
    const auto got = receiver.RecvFrom(received, sizeof(received), from);
    if (got.has_value()) {
      length = got.value();
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(length >= 0);
  REQUIRE(static_cast<std::uint32_t>(length) == built.value());
  REQUIRE(0 == std::memcmp(received, datagram, built.value()));

  const auto message = MessageView::Parse(
      osp::span<const std::uint8_t>(received, static_cast<std::size_t>(length)));
  REQUIRE(message.has_value());
  REQUIRE(message.value().IsSd());
  const auto header = SdHeaderView::Parse(message.value().Body());
  REQUIRE(header.has_value());
  SdHeaderView view = header.value();
  EntryView entry;
  REQUIRE(view.NextEntry(entry));
  REQUIRE(Entry::Kind::kFindService == entry.Kind());
  REQUIRE(kServiceId == entry.ServiceId());
}
