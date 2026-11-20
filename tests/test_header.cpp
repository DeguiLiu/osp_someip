/**
 * @file test_header.cpp
 * @brief Unit tests for the owned header and its zero-copy view.
 */

#include "osp/someip/header.hpp"

#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::ErrorCode;
using osp::someip::Header;
using osp::someip::HeaderView;
using osp::someip::MessageId;
using osp::someip::MessageType;
using osp::someip::MessageTypeField;
using osp::someip::ReturnCode;

namespace {

Header MakeHeader() {
  return Header::New(MessageId::FromServiceAndMethod(0x1234U, 0x0001U), 0xABCD0042U, 0x01U,
                     0x03U, MessageTypeField::New(MessageType::kRequest, false),
                     ReturnCode::Ok(), 0U);
}

}  // namespace

TEST_CASE("header - New computes length from the payload size", "[header]") {
  const Header header = MakeHeader();
  REQUIRE(MessageId::FromServiceAndMethod(0x1234U, 0x0001U) == header.Id());
  REQUIRE(8U == header.Length());
  REQUIRE(0U == header.PayloadSize());
  REQUIRE(0xABCD0042U == header.RequestId());
  REQUIRE(0x01U == header.ProtocolVersion());
  REQUIRE(0x03U == header.InterfaceVersion());
  REQUIRE(ReturnCode::Ok() == header.Code());
  REQUIRE_FALSE(header.IsSd());
}

TEST_CASE("header - NewSd uses the standard SD fields", "[header]") {
  const Header header = Header::NewSd(0x00000001U, 28U);
  REQUIRE(MessageId::Sd() == header.Id());
  REQUIRE(36U == header.Length());
  REQUIRE(28U == header.PayloadSize());
  REQUIRE(0x01U == header.ProtocolVersion());
  REQUIRE(0x01U == header.InterfaceVersion());
  REQUIRE(MessageType::kNotification == header.Type().Type());
  REQUIRE_FALSE(header.Type().IsTp());
  REQUIRE(header.IsSd());
}

TEST_CASE("header - NewEvent sets notification and Ok", "[header]") {
  const Header header =
      Header::NewEvent(0x005BU, 0x8001U, 0x00000007U, 0x01U, 0x02U, 4U);
  REQUIRE(MessageId::FromServiceAndMethod(0x005BU, 0x8001U) == header.Id());
  REQUIRE(12U == header.Length());
  REQUIRE(4U == header.PayloadSize());
  REQUIRE(MessageType::kNotification == header.Type().Type());
  REQUIRE(ReturnCode::Ok() == header.Code());
}

TEST_CASE("header - upper header bytes are bytes 8..16", "[header]") {
  const Header header = MakeHeader();
  const auto upper = header.UpperHeaderBytes();
  REQUIRE(0xABU == upper[0]);
  REQUIRE(0xCDU == upper[1]);
  REQUIRE(0x00U == upper[2]);
  REQUIRE(0x42U == upper[3]);
  REQUIRE(0x01U == upper[4]);
  REQUIRE(0x03U == upper[5]);
  REQUIRE(0x00U == upper[6]);
  REQUIRE(0x00U == upper[7]);
}

TEST_CASE("header - encode then parse round trips", "[header]") {
  const Header header = MakeHeader();
  std::uint8_t buf[16] = {};
  const auto written = header.EncodeTo(osp::make_span(buf));
  REQUIRE(written.has_value());
  REQUIRE(16U == written.value());

  const auto parsed = HeaderView::Parse(osp::make_span(buf));
  REQUIRE(parsed.has_value());
  REQUIRE(0U == parsed.value().rest.size());
  const HeaderView view = parsed.value().view;
  REQUIRE(view.Id() == header.Id());
  REQUIRE(view.Length() == header.Length());
  REQUIRE(view.RequestId() == header.RequestId());
  REQUIRE(view.Type() == header.Type());
  REQUIRE(view.Code() == header.Code());
  REQUIRE(view.ToHeader() == view.ToHeader());
}

TEST_CASE("header - short buffers report the missing bytes", "[header]") {
  const std::uint8_t buf[15] = {};
  const auto parsed = HeaderView::Parse(osp::make_span(buf));
  REQUIRE_FALSE(parsed.has_value());
  REQUIRE(ErrorCode::kIncomplete == parsed.get_error().code);
  REQUIRE(16U == parsed.get_error().value);
  REQUIRE(15U == parsed.get_error().available);
}

TEST_CASE("header - parse rejects bad protocol version", "[header]") {
  std::uint8_t buf[16] = {};
  const auto written = MakeHeader().EncodeTo(osp::make_span(buf));
  REQUIRE(written.has_value());
  buf[12U] = 0x02U;
  const auto parsed = HeaderView::Parse(osp::make_span(buf));
  REQUIRE_FALSE(parsed.has_value());
  REQUIRE(ErrorCode::kInvalidProtocolVersion == parsed.get_error().code);
  REQUIRE(0x02U == parsed.get_error().value);
}

TEST_CASE("header - parse rejects unknown message type and return code", "[header]") {
  std::uint8_t buf[16] = {};
  const auto written = MakeHeader().EncodeTo(osp::make_span(buf));
  REQUIRE(written.has_value());

  buf[14U] = 0x03U;
  const auto bad_type = HeaderView::Parse(osp::make_span(buf));
  REQUIRE_FALSE(bad_type.has_value());
  REQUIRE(ErrorCode::kInvalidMessageTypeField == bad_type.get_error().code);

  buf[14U] = 0x00U;
  buf[15U] = 0x5FU;
  const auto bad_code = HeaderView::Parse(osp::make_span(buf));
  REQUIRE_FALSE(bad_code.has_value());
  REQUIRE(ErrorCode::kInvalidReturnCode == bad_code.get_error().code);
}

TEST_CASE("header - short output slice is an insufficient buffer", "[header]") {
  std::uint8_t buf[8] = {};
  const auto written = MakeHeader().EncodeTo(osp::make_span(buf));
  REQUIRE_FALSE(written.has_value());
  REQUIRE(ErrorCode::kInsufficientBuffer == written.get_error().code);
  REQUIRE(16U == written.get_error().value);
  REQUIRE(8U == written.get_error().available);
}

TEST_CASE("header - set request id and raw length", "[header]") {
  Header header = Header::FromFields(MessageId::Sd(), 24U, 1U, 0x01U, 0x01U,
                                     MessageTypeField::NewSd(), ReturnCode::Ok());
  header.SetRequestId(0xDEADBEEFU);
  REQUIRE(0xDEADBEEFU == header.RequestId());
  REQUIRE(16U == header.PayloadSize());

  const Header short_length = Header::FromFields(MessageId::Sd(), 4U, 0U, 0x01U, 0x01U,
                                                 MessageTypeField::NewSd(), ReturnCode::Ok());
  REQUIRE(0U == short_length.PayloadSize());
}
