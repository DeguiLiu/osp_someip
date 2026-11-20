/**
 * @file test_wire_golden.cpp
 * @brief Byte-exact wire snapshots.
 *
 * Every expected byte array below is derived by hand from the SOME/IP wire
 * layout, not copied from encoder output. The literal is the oracle: if the
 * encoder ever disagrees, that is a wire bug to investigate.
 *
 * The Service Discovery snapshots (SD header, per-option encodings, datagram
 * builders) land with the SD milestone; this file currently pins the header
 * and the complete raw-payload message.
 */

#include "osp/someip/message.hpp"

#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::Header;
using osp::someip::Message;
using osp::someip::MessageId;
using osp::someip::MessageType;
using osp::someip::MessageTypeField;
using osp::someip::OpaquePayload;
using osp::someip::ReturnCode;

TEST_CASE("golden - request header bytes", "[golden][header]") {
  const Header header =
      Header::New(MessageId::FromServiceAndMethod(0x1234U, 0x0001U), 0xABCD0042U, 0x01U, 0x03U,
                  MessageTypeField::New(MessageType::kRequest, false), ReturnCode::Ok(), 0U);

  std::uint8_t buf[16] = {};
  const auto written = header.EncodeTo(osp::make_span(buf));
  REQUIRE(written.has_value());
  REQUIRE(16U == written.value());

  const std::uint8_t expected[16] = {
      0x12U, 0x34U, 0x00U, 0x01U,  // message_id: service 0x1234, method 0x0001
      0x00U, 0x00U, 0x00U, 0x08U,  // length = 8 (payload_len 0 + 8)
      0xABU, 0xCDU, 0x00U, 0x42U,  // request_id
      0x01U,                       // protocol_version
      0x03U,                       // interface_version
      0x00U,                       // message_type: Request, no TP
      0x00U,                       // return_code: Ok
  };
  REQUIRE(0 == std::memcmp(buf, expected, sizeof(expected)));
}

TEST_CASE("golden - response with TP flag and generic error", "[golden][header]") {
  const Header header =
      Header::New(MessageId::FromServiceAndMethod(0x005BU, 0x8001U), 0x00000007U, 0x01U, 0x02U,
                  MessageTypeField::New(MessageType::kResponse, true), ReturnCode::Generic(0x15U),
                  10U);

  std::uint8_t buf[16] = {};
  const auto written = header.EncodeTo(osp::make_span(buf));
  REQUIRE(written.has_value());

  const std::uint8_t expected[16] = {
      0x00U, 0x5BU, 0x80U, 0x01U,  // message_id: service 0x005B, method 0x8001
      0x00U, 0x00U, 0x00U, 0x12U,  // length = 18 (payload_len 10 + 8)
      0x00U, 0x00U, 0x00U, 0x07U,  // request_id
      0x01U,                       // protocol_version
      0x02U,                       // interface_version
      0xA0U,                       // message_type: Response (0x80) | TP flag (0x20)
      0x15U,                       // return_code: GenericError(0x15)
  };
  REQUIRE(0 == std::memcmp(buf, expected, sizeof(expected)));
}

TEST_CASE("golden - message with opaque payload", "[golden][message]") {
  const std::uint8_t payload_bytes[4] = {0xDEU, 0xADU, 0xBEU, 0xEFU};
  const MessageId id = MessageId::FromServiceAndMethod(0x005BU, 0x0001U);
  const Header header =
      Header::New(id, 0x00000001U, 0x01U, 0x01U, MessageTypeField::New(MessageType::kRequest, false),
                  ReturnCode::Ok(), 4U);
  const auto payload = OpaquePayload::FromPayloadBytes(id, osp::make_span(payload_bytes));
  REQUIRE(payload.has_value());
  const Message<OpaquePayload> message(header, payload.value());

  std::uint8_t buf[20] = {};
  const auto written = message.EncodeTo(osp::make_span(buf));
  REQUIRE(written.has_value());
  REQUIRE(20U == written.value());

  const std::uint8_t expected[20] = {
      0x00U, 0x5BU, 0x00U, 0x01U,  // message_id: service 0x005B, method 0x0001
      0x00U, 0x00U, 0x00U, 0x0CU,  // length = 12 (payload_len 4 + 8)
      0x00U, 0x00U, 0x00U, 0x01U,  // request_id
      0x01U,                       // protocol_version
      0x01U,                       // interface_version
      0x00U,                       // message_type: Request, no TP
      0x00U,                       // return_code: Ok
      0xDEU, 0xADU, 0xBEU, 0xEFU,  // opaque payload bytes
  };
  REQUIRE(0 == std::memcmp(buf, expected, sizeof(expected)));
}
