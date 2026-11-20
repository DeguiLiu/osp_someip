/**
 * @file test_message.cpp
 * @brief Decode validation and datagram walk behaviour.
 */

#include "osp/someip/message.hpp"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using osp::someip::ErrorCode;
using osp::someip::Header;
using osp::someip::MessageId;
using osp::someip::MessageType;
using osp::someip::MessageTypeField;
using osp::someip::MessageView;
using osp::someip::MessageWalker;
using osp::someip::ReturnCode;
using osp::someip::SdRejectReason;
using osp::someip::WalkItem;

namespace {

constexpr std::uint32_t Reason(SdRejectReason reason) {
  return static_cast<std::uint32_t>(reason);
}

std::uint32_t EncodeHeader(const Header& header, std::uint8_t* out) {
  const auto written = header.EncodeTo(osp::make_span(out, 16U));
  REQUIRE(written.has_value());
  return written.value();
}

}  // namespace

TEST_CASE("message view - payload is bounded by the length field", "[message]") {
  std::uint8_t buf[20] = {};
  const Header header = Header::New(MessageId::FromServiceAndMethod(0x005BU, 0x0001U), 1U, 0x01U,
                                    0x01U, MessageTypeField::New(MessageType::kRequest, false),
                                    ReturnCode::Ok(), 4U);
  REQUIRE(16U == EncodeHeader(header, buf));
  buf[16U] = 0xDEU;
  buf[17U] = 0xADU;
  buf[18U] = 0xBEU;
  buf[19U] = 0xEFU;

  const auto decoded = MessageView::Decode(osp::make_span(buf));
  REQUIRE(decoded.has_value());
  REQUIRE(4U == decoded.value().view.Body().size());
  REQUIRE(0xDEU == decoded.value().view.Body()[0]);
  REQUIRE(0U == decoded.value().rest.size());
  REQUIRE_FALSE(decoded.value().view.IsSd());
}

TEST_CASE("message view - bytes past the length stay in the remainder", "[message]") {
  std::uint8_t buf[24] = {};
  const Header header = Header::New(MessageId::FromServiceAndMethod(0x005BU, 0x0001U), 1U, 0x01U,
                                    0x01U, MessageTypeField::New(MessageType::kRequest, false),
                                    ReturnCode::Ok(), 4U);
  REQUIRE(16U == EncodeHeader(header, buf));
  const auto decoded = MessageView::Decode(osp::make_span(buf));
  REQUIRE(decoded.has_value());
  REQUIRE(4U == decoded.value().rest.size());
}

TEST_CASE("message view - length below eight is rejected", "[message]") {
  std::uint8_t buf[16] = {};
  const Header header = Header::FromFields(MessageId::Sd(), 7U, 0U, 0x01U, 0x01U,
                                           MessageTypeField::NewSd(), ReturnCode::Ok());
  REQUIRE(16U == EncodeHeader(header, buf));
  const auto decoded = MessageView::Decode(osp::make_span(buf));
  REQUIRE_FALSE(decoded.has_value());
  REQUIRE(ErrorCode::kInvalidLength == decoded.get_error().code);
  REQUIRE(7U == decoded.get_error().value);
}

TEST_CASE("message view - truncated payload reports the shortfall", "[message]") {
  std::uint8_t buf[18] = {};
  const Header header = Header::New(MessageId::FromServiceAndMethod(0x005BU, 0x0001U), 1U, 0x01U,
                                    0x01U, MessageTypeField::New(MessageType::kRequest, false),
                                    ReturnCode::Ok(), 4U);
  REQUIRE(16U == EncodeHeader(header, buf));
  const auto decoded = MessageView::Decode(osp::make_span(buf));
  REQUIRE_FALSE(decoded.has_value());
  REQUIRE(ErrorCode::kIncomplete == decoded.get_error().code);
  REQUIRE(4U == decoded.get_error().value);
  REQUIRE(2U == decoded.get_error().available);
}

TEST_CASE("message view - SD constraints are enforced", "[message][sd]") {
  std::uint8_t short_sd[20] = {};
  const Header short_header = Header::NewSd(0U, 4U);
  REQUIRE(16U == EncodeHeader(short_header, short_sd));
  const auto too_short = MessageView::Decode(osp::make_span(short_sd));
  REQUIRE_FALSE(too_short.has_value());
  REQUIRE(Reason(SdRejectReason::kPayloadTooShort) == too_short.get_error().value);

  std::uint8_t bad_version[28] = {};
  const Header version_header = Header::FromFields(MessageId::Sd(), 20U, 0U, 0x01U, 0x02U,
                                                   MessageTypeField::NewSd(), ReturnCode::Ok());
  REQUIRE(16U == EncodeHeader(version_header, bad_version));
  const auto version = MessageView::Decode(osp::make_span(bad_version));
  REQUIRE_FALSE(version.has_value());
  REQUIRE(Reason(SdRejectReason::kInterfaceVersion) == version.get_error().value);

  std::uint8_t bad_type[28] = {};
  const Header type_header =
      Header::FromFields(MessageId::Sd(), 20U, 0U, 0x01U, 0x01U,
                         MessageTypeField::New(MessageType::kRequest, false), ReturnCode::Ok());
  REQUIRE(16U == EncodeHeader(type_header, bad_type));
  const auto type = MessageView::Decode(osp::make_span(bad_type));
  REQUIRE_FALSE(type.has_value());
  REQUIRE(Reason(SdRejectReason::kMessageType) == type.get_error().value);

  std::uint8_t bad_code[28] = {};
  const Header code_header = Header::FromFields(MessageId::Sd(), 20U, 0U, 0x01U, 0x01U,
                                                MessageTypeField::NewSd(), ReturnCode::NotOk());
  REQUIRE(16U == EncodeHeader(code_header, bad_code));
  const auto code = MessageView::Decode(osp::make_span(bad_code));
  REQUIRE_FALSE(code.has_value());
  REQUIRE(Reason(SdRejectReason::kReturnCode) == code.get_error().value);
}

TEST_CASE("message walker - walks consecutive messages", "[message][walker]") {
  std::uint8_t datagram[40] = {};
  const Header first = Header::New(MessageId::FromServiceAndMethod(0x005BU, 0x0001U), 1U, 0x01U,
                                   0x01U, MessageTypeField::New(MessageType::kRequest, false),
                                   ReturnCode::Ok(), 4U);
  const Header second = Header::New(MessageId::FromServiceAndMethod(0x005BU, 0x0002U), 2U, 0x01U,
                                    0x01U, MessageTypeField::New(MessageType::kRequest, false),
                                    ReturnCode::Ok(), 4U);
  REQUIRE(16U == EncodeHeader(first, datagram));
  REQUIRE(16U == EncodeHeader(second, &datagram[20U]));

  MessageWalker walker(osp::make_span(datagram));
  WalkItem item;
  REQUIRE(walker.Next(item));
  REQUIRE(WalkItem::Kind::kMessage == item.kind);
  REQUIRE(0x0001U == item.message.Head().Id().MethodId());
  REQUIRE(walker.Next(item));
  REQUIRE(WalkItem::Kind::kMessage == item.kind);
  REQUIRE(0x0002U == item.message.Head().Id().MethodId());
  REQUIRE_FALSE(walker.Next(item));
  REQUIRE(0U == walker.Unparsed().size());
}

TEST_CASE("message walker - TP segments are counted and skipped", "[message][walker][tp]") {
  std::uint8_t datagram[40] = {};
  const Header tp = Header::New(MessageId::FromServiceAndMethod(0x005BU, 0x0001U), 1U, 0x01U,
                                0x01U, MessageTypeField::New(MessageType::kNotification, true),
                                ReturnCode::Ok(), 4U);
  const Header plain = Header::New(MessageId::FromServiceAndMethod(0x005BU, 0x0002U), 2U, 0x01U,
                                   0x01U, MessageTypeField::New(MessageType::kNotification, false),
                                   ReturnCode::Ok(), 4U);
  REQUIRE(16U == EncodeHeader(tp, datagram));
  REQUIRE(16U == EncodeHeader(plain, &datagram[20U]));

  MessageWalker walker(osp::make_span(datagram));
  WalkItem item;
  REQUIRE(walker.Next(item));
  REQUIRE(WalkItem::Kind::kMessage == item.kind);
  REQUIRE(0x0002U == item.message.Head().Id().MethodId());
  REQUIRE(1U == walker.SkippedTpSegments());
  REQUIRE_FALSE(walker.Next(item));
}

TEST_CASE("message walker - garbage is reported as unparsed", "[message][walker]") {
  std::uint8_t datagram[4] = {0x01U, 0x02U, 0x03U, 0x04U};
  MessageWalker walker(osp::make_span(datagram));
  WalkItem item;
  REQUIRE_FALSE(walker.Next(item));
  REQUIRE(4U == walker.Unparsed().size());
  REQUIRE(0U == walker.Remaining().size());
}

TEST_CASE("message walker - truncated TP segment ends the walk", "[message][walker][tp]") {
  std::uint8_t datagram[16] = {};
  const Header tp = Header::New(MessageId::FromServiceAndMethod(0x005BU, 0x0001U), 1U, 0x01U,
                                0x01U, MessageTypeField::New(MessageType::kNotification, true),
                                ReturnCode::Ok(), 10U);
  REQUIRE(16U == EncodeHeader(tp, datagram));

  MessageWalker walker(osp::make_span(datagram));
  WalkItem item;
  REQUIRE_FALSE(walker.Next(item));
  REQUIRE(1U == walker.SkippedTpSegments());
  REQUIRE(16U == walker.Unparsed().size());
}

TEST_CASE("message walker - malformed message carries the discarded bytes",
          "[message][walker]") {
  std::uint8_t datagram[18] = {};
  const Header header = Header::New(MessageId::FromServiceAndMethod(0x005BU, 0x0001U), 1U, 0x01U,
                                    0x01U, MessageTypeField::New(MessageType::kRequest, false),
                                    ReturnCode::Ok(), 4U);
  REQUIRE(16U == EncodeHeader(header, datagram));

  MessageWalker walker(osp::make_span(datagram));
  WalkItem item;
  REQUIRE(walker.Next(item));
  REQUIRE(WalkItem::Kind::kMalformed == item.kind);
  REQUIRE(ErrorCode::kIncomplete == item.error.code);
  REQUIRE(18U == item.bytes.size());
  REQUIRE_FALSE(walker.Next(item));
}
