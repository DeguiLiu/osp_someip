/**
 * @file test_tp.cpp
 * @brief SOME/IP-TP header, reassembly, and server integration.
 */

#include "osp/someip/message.hpp"
#include "osp/someip/message_id.hpp"
#include "osp/someip/server.hpp"
#include "osp/someip/tp.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::Header;
using osp::someip::MessageId;
using osp::someip::MessageType;
using osp::someip::MessageTypeField;
using osp::someip::MessageView;
using osp::someip::RequestReply;
using osp::someip::ReturnCode;
using osp::someip::Server;
using osp::someip::TpHeader;
using osp::someip::TpReassembly;

namespace {

constexpr std::uint16_t kServiceId = 0x1234U;
constexpr std::uint16_t kMethodId = 0x0001U;

std::uint32_t BuildSegment(std::uint8_t* out, std::uint32_t request_id,
                           std::uint32_t offset, bool more,
                           osp::span<const std::uint8_t> data) {
  const MessageId id = MessageId::FromServiceAndMethod(kServiceId, kMethodId);
  const Header header = Header::New(
      id, request_id, 0x01U, 0x01U, MessageTypeField::New(MessageType::kRequest, true),
      ReturnCode::Ok(), static_cast<std::uint32_t>(TpHeader::kSize + data.size()));
  const auto written = header.EncodeTo(osp::make_span(out, osp::someip::kHeaderSize));
  REQUIRE(written.has_value());
  REQUIRE(TpHeader::Write(
      osp::make_span(out + osp::someip::kHeaderSize, TpHeader::kSize), offset, more));
  std::memcpy(out + osp::someip::kHeaderSize + TpHeader::kSize, data.data(), data.size());
  return osp::someip::kHeaderSize + TpHeader::kSize + static_cast<std::uint32_t>(data.size());
}

MessageView ParseSegment(const std::uint8_t* bytes, std::uint32_t size) {
  const auto parsed = MessageView::Parse(osp::span<const std::uint8_t>(bytes, size));
  REQUIRE(parsed.has_value());
  return parsed.value();
}

std::array<std::uint8_t, 24U> SamplePayload(std::uint8_t first) {
  std::array<std::uint8_t, 24U> payload{};
  for (std::uint8_t index = 0U; index < payload.size(); ++index) {
    payload[index] = static_cast<std::uint8_t>(first + index);
  }
  return payload;
}

}  // namespace

TEST_CASE("tp - header round trips and rejects reserved bits", "[tp]") {
  std::uint8_t bytes[TpHeader::kSize] = {};
  REQUIRE(TpHeader::Write(osp::make_span(bytes), 32U, true));
  REQUIRE(0x21U == bytes[3U]);
  const auto parsed = TpHeader::Parse(osp::span<const std::uint8_t>(bytes));
  REQUIRE(parsed.has_value());
  REQUIRE(32U == parsed.value().offset);
  REQUIRE(parsed.value().more);

  bytes[3U] = 0x23U;
  const auto rejected = TpHeader::Parse(osp::span<const std::uint8_t>(bytes));
  REQUIRE(!rejected.has_value());
  REQUIRE(osp::someip::ErrorCode::kInvalidTpSegment == rejected.get_error().code);

  REQUIRE(!TpHeader::Write(osp::make_span(bytes), 8U, false));
}

TEST_CASE("tp - segments reassemble in order", "[tp]") {
  const auto payload = SamplePayload(0xA0U);
  TpReassembly<64U, 1U, 1000U> reassembly;
  std::uint8_t datagram[64] = {};
  MessageView out;

  const std::uint32_t first =
      BuildSegment(datagram, 1U, 0U, true, osp::make_span(payload.data(), 16U));
  REQUIRE(!reassembly.Feed(ParseSegment(datagram, first), 0U, out));
  REQUIRE(1U == reassembly.ActiveCount());

  const std::uint32_t second =
      BuildSegment(datagram, 1U, 16U, false, osp::make_span(payload.data() + 16U, 8U));
  REQUIRE(reassembly.Feed(ParseSegment(datagram, second), 1U, out));
  REQUIRE(0U == reassembly.ActiveCount());
  REQUIRE(!out.Head().Type().IsTp());
  REQUIRE(payload.size() == out.Body().size());
  REQUIRE(0 == std::memcmp(out.Body().data(), payload.data(), payload.size()));
}

TEST_CASE("tp - segments may arrive out of order", "[tp]") {
  const auto payload = SamplePayload(0xB0U);
  TpReassembly<64U, 1U, 1000U> reassembly;
  std::uint8_t datagram[128] = {};
  MessageView out;

  const std::uint32_t tail =
      BuildSegment(datagram, 1U, 16U, false, osp::make_span(payload.data() + 16U, 8U));
  REQUIRE(!reassembly.Feed(ParseSegment(datagram, tail), 0U, out));
  REQUIRE(1U == reassembly.ActiveCount());

  const std::uint32_t head =
      BuildSegment(datagram, 1U, 0U, true, osp::make_span(payload.data(), 16U));
  REQUIRE(reassembly.Feed(ParseSegment(datagram, head), 1U, out));
  REQUIRE(0U == reassembly.ActiveCount());
  REQUIRE(payload.size() == out.Body().size());
  REQUIRE(0 == std::memcmp(out.Body().data(), payload.data(), payload.size()));
}

TEST_CASE("tp - overlapping and oversized segments are dropped", "[tp]") {
  const auto payload = SamplePayload(0xC0U);
  TpReassembly<64U, 1U, 1000U> reassembly;
  std::uint8_t datagram[128] = {};
  MessageView out;

  const std::uint32_t start =
      BuildSegment(datagram, 1U, 0U, true, osp::make_span(payload.data(), 24U));
  REQUIRE(!reassembly.Feed(ParseSegment(datagram, start), 0U, out));
  REQUIRE(1U == reassembly.ActiveCount());

  // Bytes 16..24 are already stored; a segment that starts there contradicts
  // the assembly instead of completing it.
  const std::uint32_t overlap =
      BuildSegment(datagram, 1U, 16U, false, osp::make_span(payload.data() + 16U, 8U));
  REQUIRE(!reassembly.Feed(ParseSegment(datagram, overlap), 1U, out));
  REQUIRE(0U == reassembly.ActiveCount());

  const std::uint32_t beyond =
      BuildSegment(datagram, 1U, 80U, false, osp::make_span(payload.data(), 8U));
  REQUIRE(!reassembly.Feed(ParseSegment(datagram, beyond), 2U, out));
}

TEST_CASE("tp - an idle assembly times out", "[tp]") {
  const auto payload = SamplePayload(0xC0U);
  TpReassembly<64U, 1U, 100U> reassembly;
  std::uint8_t datagram[64] = {};
  MessageView out;

  const std::uint32_t start =
      BuildSegment(datagram, 1U, 0U, true, osp::make_span(payload.data(), 16U));
  REQUIRE(!reassembly.Feed(ParseSegment(datagram, start), 0U, out));
  reassembly.Tick(50U);
  REQUIRE(1U == reassembly.ActiveCount());
  reassembly.Tick(100U);
  REQUIRE(0U == reassembly.ActiveCount());
}

TEST_CASE("tp - two sessions reassemble independently", "[tp]") {
  const auto payload_a = SamplePayload(0x10U);
  const auto payload_b = SamplePayload(0x40U);
  TpReassembly<64U, 2U, 1000U> reassembly;
  std::uint8_t datagram[64] = {};
  MessageView out;

  const std::uint32_t a0 =
      BuildSegment(datagram, 1U, 0U, true, osp::make_span(payload_a.data(), 16U));
  REQUIRE(!reassembly.Feed(ParseSegment(datagram, a0), 0U, out));

  const std::uint32_t b0 =
      BuildSegment(datagram, 2U, 0U, true, osp::make_span(payload_b.data(), 16U));
  REQUIRE(!reassembly.Feed(ParseSegment(datagram, b0), 1U, out));
  REQUIRE(2U == reassembly.ActiveCount());

  const std::uint32_t a1 =
      BuildSegment(datagram, 1U, 16U, false, osp::make_span(payload_a.data() + 16U, 8U));
  REQUIRE(reassembly.Feed(ParseSegment(datagram, a1), 2U, out));
  REQUIRE(0 == std::memcmp(out.Body().data(), payload_a.data(), payload_a.size()));

  const std::uint32_t b1 =
      BuildSegment(datagram, 2U, 16U, false, osp::make_span(payload_b.data() + 16U, 8U));
  REQUIRE(reassembly.Feed(ParseSegment(datagram, b1), 3U, out));
  REQUIRE(0 == std::memcmp(out.Body().data(), payload_b.data(), payload_b.size()));
  REQUIRE(0U == reassembly.ActiveCount());
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

TEST_CASE("tp - server answers a reassembled request", "[tp][server]") {
  const auto payload = SamplePayload(0xD0U);
  CaptureSink sink;
  Server<CaptureSink, 4U, 4U, 512U, EchoHandler, osp::someip::NoE2E,
         osp::someip::NoServiceObserver, 4U, TpReassembly<128U, 1U, 1000U>>
      server(sink);
  std::uint8_t datagram[128] = {};
  const osp::someip::Ipv4Endpoint peer{{127U, 0U, 0U, 1U}, 31000U};

  const std::uint32_t first =
      BuildSegment(datagram, 1U, 0U, true, osp::make_span(payload.data(), 16U));
  server.HandleDatagram(osp::span<const std::uint8_t>(datagram, first), peer, 0U);
  REQUIRE(0U == sink.size);

  const std::uint32_t second =
      BuildSegment(datagram, 1U, 16U, false, osp::make_span(payload.data() + 16U, 8U));
  server.HandleDatagram(osp::span<const std::uint8_t>(datagram, second), peer, 1U);
  REQUIRE(sink.size > 0U);

  const auto response =
      MessageView::Parse(osp::span<const std::uint8_t>(sink.bytes, sink.size));
  REQUIRE(response.has_value());
  REQUIRE(MessageType::kResponse == response.value().Head().Type().Type());
  REQUIRE(payload.size() == response.value().Body().size());
  REQUIRE(0 == std::memcmp(response.value().Body().data(), payload.data(), payload.size()));
}
