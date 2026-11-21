/**
 * @file test_e2e_profile2.cpp
 * @brief E2E Profile 2: CRC-8/H2F, header layout and the counter state machine.
 */

#include "osp/someip/e2e_profile2.hpp"
#include "osp/someip/header.hpp"
#include "osp/someip/message.hpp"
#include "osp/someip/server.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::Crc8H2f;
using osp::someip::E2EProfile2;
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
using osp::someip::SequentialP2Ids;

namespace {

using Profile = E2EProfile2<SequentialP2Ids<0x10U>, 15U, 15U, 1U>;

const std::array<std::uint8_t, 8U> kUpperHeader{0U, 1U, 0U, 1U, 1U, 1U, 0U, 0U};

osp::span<std::uint8_t> Whole(std::array<std::uint8_t, 64U>& buffer) noexcept {
  return osp::span<std::uint8_t>(buffer.data(), buffer.size());
}

/// @brief Protect one 2-byte payload with @p sender and return the message.
std::array<std::uint8_t, 8U> ProtectOne(Profile& sender, std::uint8_t first_byte,
                                       std::size_t& size) {
  std::array<std::uint8_t, 64U> buffer{};
  buffer[0U] = first_byte;
  buffer[1U] = 0U;
  const auto body = sender.Protect(MessageId(0x12340001U), kUpperHeader, Whole(buffer), 2U);
  std::array<std::uint8_t, 8U> out{};
  std::memcpy(out.data(), body.data(), body.size());
  size = body.size();
  return out;
}

template <typename Policy>
osp::someip::E2ECheck CheckBytes(Policy& receiver, const std::array<std::uint8_t, 8U>& bytes,
                        std::size_t size) {
  return receiver.Check(MessageId(0x12340001U), kUpperHeader,
                        osp::span<const std::uint8_t>(bytes.data(), size));
}

}  // namespace

TEST_CASE("e2e p2 - CRC-8/H2F catalogue check value", "[e2e][profile2]") {
  const std::array<std::uint8_t, 9U> text{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  Crc8H2f crc;
  crc.Update(osp::span<const std::uint8_t>(text.data(), text.size()));
  REQUIRE(0xDFU == crc.Value());
}

TEST_CASE("e2e p2 - protect starts at counter one and round trips", "[e2e][profile2]") {
  Profile sender;
  std::size_t size = 0U;
  const auto message = ProtectOne(sender, 0xA5U, size);
  REQUIRE(4U == size);
  REQUIRE(1U == (message[1U] & 0x0FU));

  Profile receiver;
  const auto check = CheckBytes(receiver, message, size);
  REQUIRE(check.ok);
  REQUIRE(E2EStatus::kOk == check.status);
  REQUIRE(2U == check.payload.size());
  REQUIRE(0xA5U == check.payload[0U]);
}

TEST_CASE("e2e p2 - counters drive ok, lost, repeated and resync", "[e2e][profile2]") {
  Profile sender;
  Profile receiver;
  std::array<std::array<std::uint8_t, 8U>, 8U> messages{};
  std::array<std::size_t, 8U> sizes{};
  for (std::size_t index = 0U; index < messages.size(); ++index) {
    messages[index] = ProtectOne(sender, static_cast<std::uint8_t>(index), sizes[index]);
  }
  REQUIRE(1U == (messages[0][1U] & 0x0FU));
  REQUIRE(3U == (messages[2][1U] & 0x0FU));
  REQUIRE(6U == (messages[5][1U] & 0x0FU));

  REQUIRE(E2EStatus::kOk == CheckBytes(receiver, messages[0], sizes[0]).status);
  REQUIRE(E2EStatus::kOk == CheckBytes(receiver, messages[1], sizes[1]).status);
  REQUIRE(E2EStatus::kOk == CheckBytes(receiver, messages[2], sizes[2]).status);
  REQUIRE(E2EStatus::kOkSomeLost == CheckBytes(receiver, messages[5], sizes[5]).status);
  const auto repeated = CheckBytes(receiver, messages[5], sizes[5]);
  REQUIRE(repeated.ok);
  REQUIRE(E2EStatus::kRepeated == repeated.status);

  // A counter outside the window is rejected and starts a resync. The window
  // must be smaller than the counter range for that case to exist.
  E2EProfile2<SequentialP2Ids<0x10U>, 2U, 15U, 1U> narrow;
  REQUIRE(E2EStatus::kOk == CheckBytes(narrow, messages[0], sizes[0]).status);
  const auto wrong = CheckBytes(narrow, messages[5], sizes[5]);
  REQUIRE(!wrong.ok);
  REQUIRE(E2EStatus::kWrongSequence == wrong.status);
  // With SyncCounterInit = 1 the next valid message is still in sync state.
  const auto syncing = CheckBytes(narrow, messages[6], sizes[6]);
  REQUIRE(!syncing.ok);
  REQUIRE(E2EStatus::kSync == syncing.status);
  // The receiver accepts data again once the sync counter is used up.
  const auto recovered = CheckBytes(narrow, messages[7], sizes[7]);
  REQUIRE(recovered.ok);
  REQUIRE(E2EStatus::kOk == recovered.status);
  (void)receiver;
}

TEST_CASE("e2e p2 - a corrupted CRC is rejected", "[e2e][profile2]") {
  Profile sender;
  Profile receiver;
  std::size_t size = 0U;
  auto message = ProtectOne(sender, 0x11U, size);
  message[0U] = static_cast<std::uint8_t>(message[0U] ^ 0x01U);
  const auto check = CheckBytes(receiver, message, size);
  REQUIRE(!check.ok);
  REQUIRE(E2EStatus::kError == check.status);
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

TEST_CASE("e2e p2 - server checks the request and protects the reply", "[e2e][profile2]") {
  CaptureSink sink;
  osp::someip::Server<CaptureSink, 2U, 2U, 256U, EchoHandler, Profile> server(sink);
  const osp::someip::Ipv4Endpoint peer{{127U, 0U, 0U, 1U}, 31000U};

  Profile sender;
  Profile receiver;
  std::size_t size = 0U;
  auto message = ProtectOne(sender, 0x5AU, size);
  message[0U] = message[0U];

  const Header header = Header::New(
      MessageId::FromServiceAndMethod(0x1234U, 0x0001U), 0x00010001U, 0x01U, 0x01U,
      MessageTypeField::New(MessageType::kRequest, false), ReturnCode::Ok(),
      static_cast<std::uint32_t>(size));
  const Message<OpaquePayload> request(
      header, OpaquePayload(osp::span<const std::uint8_t>(message.data(), size)));
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
  REQUIRE(2U == check.payload.size());
  REQUIRE(0x5AU == check.payload[0U]);
}
