/**
 * @file test_e2e_profile7.cpp
 * @brief E2E Profile 7: CRC-64 check value, header layout, counter window.
 */

#include "osp/someip/e2e_profile7.hpp"
#include "osp/someip/header.hpp"
#include "osp/someip/message.hpp"
#include "osp/someip/server.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::Crc64Xz;
using osp::someip::E2EProfile7;
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

constexpr std::uint32_t kDataId = 0x00001234U;
using Profile = E2EProfile7<kDataId, 15U>;

const std::array<std::uint8_t, 8U> kUpperHeader{0U, 1U, 0U, 1U, 1U, 1U, 0U, 0U};

std::uint32_t ReadU32(const std::uint8_t* bytes, std::size_t at) {
  return (static_cast<std::uint32_t>(bytes[at]) << 24U) |
         (static_cast<std::uint32_t>(bytes[at + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[at + 2U]) << 8U) | bytes[at + 3U];
}

osp::span<std::uint8_t> Whole(std::array<std::uint8_t, 64U>& buffer) noexcept {
  return osp::span<std::uint8_t>(buffer.data(), buffer.size());
}

}  // namespace

TEST_CASE("e2e p7 - CRC-64/XZ catalogue check value", "[e2e][profile7]") {
  const std::array<std::uint8_t, 9U> text{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  Crc64Xz crc;
  crc.Update(osp::span<const std::uint8_t>(text.data(), text.size()));
  REQUIRE(0x995DC9BBDF1939FAULL == crc.Value());
}

TEST_CASE("e2e p7 - protect writes the header and check returns the payload", "[e2e][profile7]") {
  Profile sender;
  Profile receiver;
  std::array<std::uint8_t, 64U> buffer{};
  const std::array<std::uint8_t, 5U> data{'h', 'e', 'l', 'l', 'o'};
  std::memcpy(buffer.data(), data.data(), data.size());

  const auto protected_payload =
      sender.Protect(MessageId(0x12340001U), kUpperHeader, Whole(buffer), data.size());
  REQUIRE(data.size() + Profile::kHeaderSize == protected_payload.size());
  REQUIRE(static_cast<std::uint32_t>(protected_payload.size()) ==
          ReadU32(protected_payload.data(), 8U));
  REQUIRE(0U == ReadU32(protected_payload.data(), 12U));
  REQUIRE(kDataId == ReadU32(protected_payload.data(), 16U));

  const auto check = receiver.Check(MessageId(0x12340001U), kUpperHeader, protected_payload);
  REQUIRE(check.applies);
  REQUIRE(check.ok);
  REQUIRE(data.size() == check.payload.size());
  REQUIRE(0 == std::memcmp(check.payload.data(), data.data(), data.size()));
}

TEST_CASE("e2e p7 - tampering, length and data id are checked", "[e2e][profile7]") {
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

  auto wrong_length = protected_payload;
  wrong_length[11U] = static_cast<std::uint8_t>(wrong_length[11U] + 1U);
  REQUIRE(!receiver.Check(MessageId(0x12340001U), kUpperHeader, wrong_length).ok);

  E2EProfile7<0x4321U, 15U> other_sender;
  std::array<std::uint8_t, 64U> other_buffer{};
  std::memcpy(other_buffer.data(), data.data(), data.size());
  const auto other_payload =
      other_sender.Protect(MessageId(0x43210001U), kUpperHeader, Whole(other_buffer), data.size());
  REQUIRE(!receiver.Check(MessageId(0x43210001U), kUpperHeader, other_payload).ok);
}

TEST_CASE("e2e p7 - counter repeats are accepted and large jumps are not", "[e2e][profile7]") {
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

  REQUIRE(0U == ReadU32(first.data(), 12U));
  REQUIRE(1U == ReadU32(second.data(), 12U));
  REQUIRE(2U == ReadU32(third.data(), 12U));

  REQUIRE(receiver.Check(MessageId(0x12340001U), kUpperHeader, first).ok);
  REQUIRE(receiver.Check(MessageId(0x12340001U), kUpperHeader, second).ok);
  // A repeat of the same counter is a duplicated message, not a sequence loss.
  REQUIRE(receiver.Check(MessageId(0x12340001U), kUpperHeader, second).ok);

  E2EProfile7<kDataId, 1U> narrow;
  REQUIRE(narrow.Check(MessageId(0x12340001U), kUpperHeader, first).ok);
  REQUIRE(!narrow.Check(MessageId(0x12340001U), kUpperHeader, third).ok);
}

namespace {

struct CaptureSink final {
  std::uint8_t bytes[512] = {};
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

TEST_CASE("e2e p7 - server checks the request and protects the reply", "[e2e][profile7]") {
  CaptureSink sink;
  osp::someip::Server<CaptureSink, 2U, 2U, 512U, EchoHandler, Profile> server(sink);
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
  std::uint8_t datagram[128] = {};
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

TEST_CASE("e2e p7 - counters wrap from 0xFFFFFFFF to 0", "[e2e][profile7]") {
  Profile sender;
  Profile receiver;
  std::array<std::uint8_t, 64U> buffer{};
  const std::array<std::uint8_t, 2U> data{1U, 2U};
  std::memcpy(buffer.data(), data.data(), data.size());
  const auto first =
      sender.Protect(MessageId(0x12340001U), kUpperHeader, Whole(buffer), data.size());
  const std::size_t size = first.size();
  std::array<std::uint8_t, 32U> low{};
  std::memcpy(low.data(), first.data(), size);

  std::array<std::uint8_t, 32U> high = low;
  high[12U] = 0xFFU;
  high[13U] = 0xFFU;
  high[14U] = 0xFFU;
  high[15U] = 0xFFU;
  for (std::size_t index = 0U; index < 8U; ++index) {
    high[index] = 0U;
  }
  osp::someip::Crc64Xz crc;
  crc.Update(osp::span<const std::uint8_t>(high.data() + 8U, size - 8U));
  const std::uint64_t value = crc.Value();
  for (std::size_t index = 0U; index < 8U; ++index) {
    high[index] = static_cast<std::uint8_t>(value >> ((7U - index) * 8U));
  }

  REQUIRE(receiver.Check(MessageId(0x12340001U), kUpperHeader,
                         osp::span<const std::uint8_t>(high.data(), size)).ok);
  REQUIRE(receiver.Check(MessageId(0x12340001U), kUpperHeader,
                         osp::span<const std::uint8_t>(low.data(), size)).ok);
}
