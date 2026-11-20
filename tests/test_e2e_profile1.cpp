/**
 * @file test_e2e_profile1.cpp
 * @brief E2E Profile 1 CRC, counter checks, and a server round trip.
 */

#include "osp/someip/e2e_profile1.hpp"
#include "osp/someip/header.hpp"
#include "osp/someip/message.hpp"
#include "osp/someip/server.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::Crc8SaeJ1850;
using osp::someip::E2EProfile1;
using osp::someip::E2EStatus;
using osp::someip::Header;
using osp::someip::Message;
using osp::someip::MessageId;
using osp::someip::MessageType;
using osp::someip::MessageTypeField;
using osp::someip::MessageView;
using osp::someip::OpaquePayload;
using osp::someip::RequestReply;
using osp::someip::ReturnCode;

namespace {

constexpr std::uint16_t kDataId = 0x1234U;
using Profile = E2EProfile1<kDataId>;

const std::array<std::uint8_t, 8U> kUpperHeader{0x00U, 0x01U, 0x00U, 0x01U,
                                                0x01U, 0x01U, 0x00U, 0x00U};

template <std::size_t N>
osp::span<std::uint8_t> WholeBuffer(std::array<std::uint8_t, N>& buffer) noexcept {
  return osp::span<std::uint8_t>(buffer.data(), N);
}

}  // namespace

TEST_CASE("e2e p1 - CRC-8 SAE J1850 check value", "[e2e][profile1]") {
  const std::array<std::uint8_t, 9U> text{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  Crc8SaeJ1850 crc;
  crc.Update(osp::span<const std::uint8_t>(text.data(), text.size()));
  REQUIRE(0x4BU == crc.Value());
}

TEST_CASE("e2e p1 - protect then check returns the payload", "[e2e][profile1]") {
  Profile sender;
  Profile receiver;
  std::array<std::uint8_t, 16U> buffer{};
  const std::array<std::uint8_t, 5U> data{'h', 'e', 'l', 'l', 'o'};
  std::memcpy(buffer.data(), data.data(), data.size());

  const auto protected_payload =
      sender.Protect(MessageId(0x12340001U), kUpperHeader, WholeBuffer(buffer), data.size());
  REQUIRE(data.size() + Profile::kHeaderSize == protected_payload.size());

  const auto check = receiver.Check(MessageId(0x12340001U), kUpperHeader, protected_payload);
  REQUIRE(check.applies);
  REQUIRE(check.ok);
  REQUIRE(data.size() == check.payload.size());
  REQUIRE(0 == std::memcmp(check.payload.data(), data.data(), data.size()));
}

TEST_CASE("e2e p1 - a tampered payload fails the check", "[e2e][profile1]") {
  Profile sender;
  Profile receiver;
  std::array<std::uint8_t, 16U> buffer{};
  const std::array<std::uint8_t, 5U> data{'h', 'e', 'l', 'l', 'o'};
  std::memcpy(buffer.data(), data.data(), data.size());
  auto protected_payload =
      sender.Protect(MessageId(0x12340001U), kUpperHeader, WholeBuffer(buffer), data.size());

  protected_payload[3U] = static_cast<std::uint8_t>(protected_payload[3U] ^ 0x01U);
  const auto check = receiver.Check(MessageId(0x12340001U), kUpperHeader, protected_payload);
  REQUIRE(check.applies);
  REQUIRE(!check.ok);
}

TEST_CASE("e2e p1 - a repeated counter is rejected", "[e2e][profile1]") {
  Profile sender;
  Profile receiver;
  std::array<std::uint8_t, 16U> buffer{};
  const std::array<std::uint8_t, 3U> data{1U, 2U, 3U};
  std::memcpy(buffer.data(), data.data(), data.size());
  const auto protected_payload =
      sender.Protect(MessageId(0x12340001U), kUpperHeader, WholeBuffer(buffer), data.size());

  REQUIRE(receiver.Check(MessageId(0x12340001U), kUpperHeader, protected_payload).ok);
  REQUIRE(!receiver.Check(MessageId(0x12340001U), kUpperHeader, protected_payload).ok);
}

namespace {

struct CaptureSink final {
  std::uint8_t bytes[256] = {};
  std::size_t size{0U};

  void operator()(osp::span<const std::uint8_t> datagram,
                  const osp::someip::Ipv4Endpoint& /*to*/) noexcept {
    size = datagram.size();
    std::memcpy(bytes, datagram.data(), datagram.size());
  }
};

struct EchoHandler final {
  RequestReply OnRequest(const osp::someip::IncomingRequest& request) noexcept {
    return RequestReply{true, false, ReturnCode::Ok(), request.payload};
  }
};

}  // namespace

TEST_CASE("e2e p1 - server checks the request and protects the reply", "[e2e][profile1]") {
  CaptureSink sink;
  osp::someip::Server<CaptureSink, 2U, 2U, 256U, EchoHandler, Profile> server(sink);
  const osp::someip::Ipv4Endpoint peer{{127U, 0U, 0U, 1U}, 31000U};

  Profile sender;
  Profile receiver;
  std::array<std::uint8_t, 32U> payload_buffer{};
  const std::array<std::uint8_t, 4U> data{0xDEU, 0xADU, 0xBEU, 0xEFU};
  std::memcpy(payload_buffer.data(), data.data(), data.size());
  const auto protected_payload = sender.Protect(
      MessageId(0x12340001U), kUpperHeader, WholeBuffer(payload_buffer), data.size());

  const Header header = Header::New(
      MessageId::FromServiceAndMethod(0x1234U, 0x0001U), 0x00010001U, 0x01U, 0x01U,
      MessageTypeField::New(MessageType::kRequest, false), ReturnCode::Ok(),
      static_cast<std::uint32_t>(protected_payload.size()));
  const Message<OpaquePayload> request(header, OpaquePayload(protected_payload));
  std::uint8_t datagram[64] = {};
  const auto written = request.EncodeTo(osp::make_span(datagram));
  REQUIRE(written.has_value());

  server.HandleDatagram(osp::span<const std::uint8_t>(datagram, written.value()), peer, 0U);
  REQUIRE(sink.size > 0U);

  const auto response = MessageView::Parse(osp::span<const std::uint8_t>(sink.bytes, sink.size));
  REQUIRE(response.has_value());
  const auto check =
      receiver.Check(response.value().Head().Id(), kUpperHeader, response.value().Body());
  REQUIRE(check.ok);
  REQUIRE(data.size() == check.payload.size());
  REQUIRE(0 == std::memcmp(check.payload.data(), data.data(), data.size()));
}
