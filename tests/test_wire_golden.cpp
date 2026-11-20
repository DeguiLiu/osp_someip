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
#include "osp/someip/sd_codec.hpp"

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
using osp::someip::OfferServiceRequest;
using osp::someip::SubscribeAckRequest;
using osp::someip::SubscribeEventGroupRequest;

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

TEST_CASE("golden - subscribe event group datagram", "[golden][sd][codec]") {
  SubscribeEventGroupRequest request;
  request.service_id = 0x1234U;
  request.instance_id = 0x0001U;
  request.major_version = 1U;
  request.event_group_id = 0x0001U;
  request.ttl = 5U;
  request.local_ip = {10U, 0U, 0U, 5U};
  request.local_rx_port = 30509U;

  auto counter = osp::someip::sd::SdSessionCounter::Resume(
      6U, osp::someip::sd::RebootFlag::kContinuous);
  const auto session = counter.Next();
  REQUIRE(7U == session.id);

  std::uint8_t buf[64] = {};
  const auto written = osp::someip::BuildSubscribeEventGroupDatagram(
      osp::make_span(buf), request, session);
  REQUIRE(written.has_value());
  REQUIRE(56U == written.value());

  const std::uint8_t expected[56] = {
      0xFFU, 0xFFU, 0x81U, 0x00U,  // message_id: SD
      0x00U, 0x00U, 0x00U, 0x30U,  // length = 48 (SD payload 40 + 8)
      0x00U, 0x00U, 0x00U, 0x07U,  // request_id = session 7
      0x01U, 0x01U,                // protocol/interface version
      0x02U, 0x00U,                // Notification, Ok
      0x40U, 0x00U, 0x00U, 0x00U,  // flags: unicast only (Continuous)
      0x00U, 0x00U, 0x00U, 0x10U,  // entries_size = 16
      0x06U, 0x00U, 0x00U, 0x10U,  // Subscribe, counts first=1
      0x12U, 0x34U, 0x00U, 0x01U,  // service 0x1234, instance 1
      0x01U, 0x00U, 0x00U, 0x05U,  // major 1, ttl 5
      0x00U, 0x00U, 0x00U, 0x01U,  // counter 0, event group 1
      0x00U, 0x00U, 0x00U, 0x0CU,  // options_size = 12
      0x00U, 0x09U, 0x04U, 0x00U,  // option length 9, IPv4 endpoint
      10U,   0U,    0U,    5U,     // 10.0.0.5
      0x00U, 0x11U, 0x77U, 0x2DU,  // reserved, UDP, port 30509
  };
  REQUIRE(0 == std::memcmp(buf, expected, sizeof(expected)));
}

TEST_CASE("golden - offer service datagram", "[golden][sd][codec]") {
  OfferServiceRequest request;
  request.service_id = 0x0042U;
  request.instance_id = 0x0001U;
  request.major_version = 1U;
  request.minor_version = 0U;
  request.ttl = 3U;
  request.local_ip = {192U, 0U, 2U, 1U};
  request.unicast_port = 30501U;

  auto counter = osp::someip::sd::SdSessionCounter::Resume(
      0U, osp::someip::sd::RebootFlag::kContinuous);
  const auto session = counter.Next();
  REQUIRE(1U == session.id);

  std::uint8_t buf[64] = {};
  const auto written =
      osp::someip::BuildOfferServiceDatagram(osp::make_span(buf), request, session);
  REQUIRE(written.has_value());
  REQUIRE(56U == written.value());

  const std::uint8_t expected[56] = {
      0xFFU, 0xFFU, 0x81U, 0x00U,  // message_id: SD
      0x00U, 0x00U, 0x00U, 0x30U,  // length = 48
      0x00U, 0x00U, 0x00U, 0x01U,  // request_id = session 1
      0x01U, 0x01U, 0x02U, 0x00U,  // versions, Notification, Ok
      0x40U, 0x00U, 0x00U, 0x00U,  // flags: unicast only
      0x00U, 0x00U, 0x00U, 0x10U,  // entries_size = 16
      0x01U, 0x00U, 0x00U, 0x10U,  // OfferService, counts first=1
      0x00U, 0x42U, 0x00U, 0x01U,  // service 0x0042, instance 1
      0x01U, 0x00U, 0x00U, 0x03U,  // major 1, ttl 3
      0x00U, 0x00U, 0x00U, 0x00U,  // minor version 0
      0x00U, 0x00U, 0x00U, 0x0CU,  // options_size = 12
      0x00U, 0x09U, 0x04U, 0x00U,  // option length 9, IPv4 endpoint
      192U,  0U,    2U,    1U,     // 192.0.2.1
      0x00U, 0x11U, 0x77U, 0x25U,  // reserved, UDP, port 30501
  };
  REQUIRE(0 == std::memcmp(buf, expected, sizeof(expected)));
}

TEST_CASE("golden - subscribe ack datagram", "[golden][sd][codec]") {
  SubscribeAckRequest request;
  request.service_id = 0x0099U;
  request.instance_id = 2U;
  request.event_group_id = 0x0005U;
  request.major_version = 1U;
  request.ttl = 10U;
  request.counter = 0U;

  auto counter = osp::someip::sd::SdSessionCounter::Resume(
      1U, osp::someip::sd::RebootFlag::kContinuous);
  const auto session = counter.Next();
  REQUIRE(2U == session.id);

  std::uint8_t buf[64] = {};
  const auto written =
      osp::someip::BuildSubscribeAckDatagram(osp::make_span(buf), request, session);
  REQUIRE(written.has_value());
  REQUIRE(44U == written.value());

  const std::uint8_t expected[44] = {
      0xFFU, 0xFFU, 0x81U, 0x00U,  // message_id: SD
      0x00U, 0x00U, 0x00U, 0x24U,  // length = 36 (SD payload 28 + 8)
      0x00U, 0x00U, 0x00U, 0x02U,  // request_id = session 2
      0x01U, 0x01U, 0x02U, 0x00U,  // versions, Notification, Ok
      0x40U, 0x00U, 0x00U, 0x00U,  // flags: unicast only
      0x00U, 0x00U, 0x00U, 0x10U,  // entries_size = 16
      0x07U, 0x00U, 0x00U, 0x00U,  // SubscribeAck, no options
      0x00U, 0x99U, 0x00U, 0x02U,  // service 0x0099, instance 2
      0x01U, 0x00U, 0x00U, 0x0AU,  // major 1, ttl 10
      0x00U, 0x00U, 0x00U, 0x05U,  // counter 0, event group 5
      0x00U, 0x00U, 0x00U, 0x00U,  // options_size = 0
  };
  REQUIRE(0 == std::memcmp(buf, expected, sizeof(expected)));
}
