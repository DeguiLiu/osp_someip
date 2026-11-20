/**
 * @file test_owned_payload.cpp
 * @brief Owning fixed-capacity payload: copy semantics, capacity and encoding.
 */

#include "osp/someip/message.hpp"
#include "osp/someip/owned_payload.hpp"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using osp::someip::ErrorCode;
using osp::someip::Header;
using osp::someip::Message;
using osp::someip::MessageId;
using osp::someip::MessageType;
using osp::someip::MessageTypeField;
using osp::someip::OwnedPayload;
using osp::someip::ReturnCode;

TEST_CASE("owned payload - copies the source bytes", "[payload]") {
  std::uint8_t source[3] = {0x01U, 0x02U, 0x03U};
  auto payload = OwnedPayload<8U>::FromPayloadBytes(MessageId(1U), osp::make_span(source));
  REQUIRE(payload.has_value());
  REQUIRE(3U == payload.value().Size());

  source[0U] = 0xFFU;  // the payload must not observe this
  REQUIRE(0x01U == payload.value().Bytes()[0]);

  std::uint8_t buffer[8] = {};
  const auto written = payload.value().EncodeTo(osp::make_span(buffer));
  REQUIRE(written.has_value());
  REQUIRE(3U == written.value());
  REQUIRE(0x01U == buffer[0]);
  REQUIRE(0x02U == buffer[1]);
  REQUIRE(0x03U == buffer[2]);
}

TEST_CASE("owned payload - capacity is enforced", "[payload]") {
  const std::uint8_t source[9] = {};
  const auto payload = OwnedPayload<8U>::FromPayloadBytes(MessageId(1U), osp::make_span(source));
  REQUIRE_FALSE(payload.has_value());
  REQUIRE(ErrorCode::kInsufficientBuffer == payload.get_error().code);
  REQUIRE(8U == payload.get_error().value);
  REQUIRE(9U == payload.get_error().available);
}

TEST_CASE("owned payload - encodes inside a message", "[payload]") {
  const std::uint8_t source[3] = {0xAAU, 0xBBU, 0xCCU};
  const auto payload = OwnedPayload<8U>::FromPayloadBytes(MessageId(1U), osp::make_span(source));
  REQUIRE(payload.has_value());

  const MessageId id = MessageId::FromServiceAndMethod(0x005BU, 0x0001U);
  const Header header =
      Header::New(id, 1U, 0x01U, 0x01U, MessageTypeField::New(MessageType::kRequest, false),
                  ReturnCode::Ok(), payload.value().Size());
  const Message<OwnedPayload<8U>> message(header, payload.value());
  REQUIRE(19U == message.EncodedSize());

  std::uint8_t buffer[19] = {};
  const auto written = message.EncodeTo(osp::make_span(buffer));
  REQUIRE(written.has_value());
  REQUIRE(19U == written.value());
  REQUIRE(0x0BU == buffer[7U]);  // length = 11 (payload 3 + 8)
  REQUIRE(0xAAU == buffer[16U]);
  REQUIRE(0xCCU == buffer[18U]);
}
