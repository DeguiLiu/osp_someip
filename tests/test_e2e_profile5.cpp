/**
 * @file test_e2e_profile5.cpp
 * @brief E2E Profile 5: CRC-16 check value, header layout, counter window.
 */

#include "osp/someip/e2e_profile5.hpp"
#include "osp/someip/header.hpp"
#include "osp/someip/message.hpp"
#include "osp/someip/server.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::Crc16CcittFalse;
using osp::someip::E2EProfile5;
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
using Profile = E2EProfile5<kDataId, 15U>;

const std::array<std::uint8_t, 8U> kUpperHeader{0U, 1U, 0U, 1U, 1U, 1U, 0U, 0U};

osp::span<std::uint8_t> Whole(std::array<std::uint8_t, 64U>& buffer) noexcept {
  return osp::span<std::uint8_t>(buffer.data(), buffer.size());
}

}  // namespace

TEST_CASE("e2e p5 - CRC-16/CCITT-FALSE catalogue check value", "[e2e][profile5]") {
  const std::array<std::uint8_t, 9U> text{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  Crc16CcittFalse crc;
  crc.Update(osp::span<const std::uint8_t>(text.data(), text.size()));
  REQUIRE(0x29B1U == crc.Value());
}

TEST_CASE("e2e p5 - protect writes the header and check returns the payload", "[e2e][profile5]") {
  Profile sender;
  Profile receiver;
  std::array<std::uint8_t, 64U> buffer{};
  const std::array<std::uint8_t, 5U> data{'h', 'e', 'l', 'l', 'o'};
  std::memcpy(buffer.data(), data.data(), data.size());

  const auto protected_payload =
      sender.Protect(MessageId(0x12340001U), kUpperHeader, Whole(buffer), data.size());
  REQUIRE(data.size() + Profile::kHeaderSize == protected_payload.size());
  REQUIRE(0U == protected_payload[2U]);

  const auto check = receiver.Check(MessageId(0x12340001U), kUpperHeader, protected_payload);
  REQUIRE(check.applies);
  REQUIRE(check.ok);
  REQUIRE(data.size() == check.payload.size());
  REQUIRE(0 == std::memcmp(check.payload.data(), data.data(), data.size()));
}

TEST_CASE("e2e p5 - tampering and short payloads are rejected", "[e2e][profile5]") {
  Profile sender;
  Profile receiver;
  std::array<std::uint8_t, 64U> buffer{};
  const std::array<std::uint8_t, 4U> data{1U, 2U, 3U, 4U};
  std::memcpy(buffer.data(), data.data(), data.size());
  const auto protected_payload =
      sender.Protect(MessageId(0x12340001U), kUpperHeader, Whole(buffer), data.size());

  auto tampered = protected_payload;
  tampered[tampered.size() - 1U] = static_cast<std::uint8_t>(tampered[tampered.size() - 1U] ^ 0x01U);
  REQUIRE(!receiver.Check(MessageId(0x12340001U), kUpperHeader, tampered).ok);

  const std::array<std::uint8_t, 2U> too_short{0U, 0U};
  REQUIRE(!receiver.Check(MessageId(0x12340001U), kUpperHeader,
                          osp::span<const std::uint8_t>(too_short.data(), too_short.size())).ok);
}

TEST_CASE("e2e p5 - counter repeats are accepted and large jumps are not", "[e2e][profile5]") {
  Profile sender;
  Profile receiver;
  const std::array<std::uint8_t, 2U> data{7U, 8U};
  std::array<std::uint8_t, 64U> b0{};
  std::array<std::uint8_t, 64U> b1{};
  std::array<std::uint8_t, 64U> b2{};
  std::memcpy(b0.data(), data.data(), data.size());
  std::memcpy(b1.data(), data.data(), data.size());
  std::memcpy(b2.data(), data.data(), data.size());
  const auto first = sender.Protect(MessageId(0x12340001U), kUpperHeader, Whole(b0), data.size());
  const auto second = sender.Protect(MessageId(0x12340001U), kUpperHeader, Whole(b1), data.size());
  const auto third = sender.Protect(MessageId(0x12340001U), kUpperHeader, Whole(b2), data.size());

  REQUIRE(0U == first[2U]);
  REQUIRE(1U == second[2U]);
  REQUIRE(2U == third[2U]);

  REQUIRE(receiver.Check(MessageId(0x12340001U), kUpperHeader, first).ok);
  REQUIRE(receiver.Check(MessageId(0x12340001U), kUpperHeader, second).ok);
  REQUIRE(receiver.Check(MessageId(0x12340001U), kUpperHeader, second).ok);  // repeat

  E2EProfile5<kDataId, 1U> narrow;
  REQUIRE(narrow.Check(MessageId(0x12340001U), kUpperHeader, first).ok);
  REQUIRE(!narrow.Check(MessageId(0x12340001U), kUpperHeader, third).ok);
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

TEST_CASE("e2e p5 - server checks the request and protects the reply", "[e2e][profile5]") {
  CaptureSink sink;
  osp::someip::Server<CaptureSink, 2U, 2U, 256U, EchoHandler, Profile> server(sink);
  const osp::someip::Ipv4Endpoint peer{{127U, 0U, 0U, 1U}, 31000U};

  Profile sender;
  Profile receiver;
  std::array<std::uint8_t, 64U> payload_buffer{};
  const std::array<std::uint8_t, 4U> data{0xDEU, 0xADU, 0xBEU, 0xEFU};
  std::memcpy(payload_buffer.data(), data.data(), data.size());
  const auto protected_payload =
      sender.Protect(MessageId(0x12340001U), kUpperHeader, Whole(payload_buffer), data.size());

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
