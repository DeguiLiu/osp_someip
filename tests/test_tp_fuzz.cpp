/**
 * @file test_tp_fuzz.cpp
 * @brief Deterministic random-input sweep over the TP reassembler.
 *
 * Any segment sequence may be rejected, but the reassembler must never write
 * outside its buffer, never report more than MaxSessions assemblies, and never
 * hand back a message larger than MaxPayload. Run under ASan/UBSan for value.
 */

#include "osp/someip/message.hpp"
#include "osp/someip/message_id.hpp"
#include "osp/someip/tp.hpp"

#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::Header;
using osp::someip::MessageId;
using osp::someip::MessageType;
using osp::someip::MessageTypeField;
using osp::someip::MessageView;
using osp::someip::ReturnCode;
using osp::someip::TpHeader;
using osp::someip::TpReassembly;

namespace {

constexpr std::uint32_t kMaxPayload = 128U;
constexpr std::uint32_t kMaxSessions = 4U;

std::uint32_t NextRandom(std::uint32_t& state) noexcept {
  state = (state * 1664525U) + 1013904223U;
  return state >> 16U;
}

/// @brief Encode one TP segment; returns its length.
std::uint32_t BuildSegment(std::uint8_t* out, std::uint32_t request_id, std::uint32_t offset,
                           bool more, std::uint32_t length, std::uint8_t fill,
                           bool aligned) noexcept {
  const MessageId id = MessageId::FromServiceAndMethod(0x1234U, 0x0001U);
  const Header header = Header::New(
      id, request_id, 0x01U, 0x01U, MessageTypeField::New(MessageType::kNotification, true),
      ReturnCode::Ok(), TpHeader::kSize + length);
  const std::uint32_t written_offset = aligned ? offset : offset + 1U;
  (void)header.EncodeTo(osp::make_span(out, osp::someip::kHeaderSize));
  (void)TpHeader::Write(
      osp::make_span(out + osp::someip::kHeaderSize, TpHeader::kSize), written_offset, more);
  std::uint8_t* data = out + osp::someip::kHeaderSize + TpHeader::kSize;
  std::memset(data, fill, length);
  return osp::someip::kHeaderSize + TpHeader::kSize + length;
}

}  // namespace

TEST_CASE("tp fuzz - random segments never escape the reassembler", "[fuzz][tp]") {
  TpReassembly<kMaxPayload, kMaxSessions, 50U> reassembly;
  std::uint8_t datagram[256] = {};
  std::uint32_t state = 0x5EED1234U;

  for (std::uint32_t round = 0U; round < 4000U; ++round) {
    const std::uint32_t request_id = 1U + (NextRandom(state) % 6U);
    const bool aligned = 0U == (NextRandom(state) % 8U);
    const std::uint32_t offset = (NextRandom(state) % (kMaxPayload / TpHeader::kOffsetUnit)) *
                                 TpHeader::kOffsetUnit;
    const std::uint32_t length = NextRandom(state) % 64U;
    const bool more = 0U != (NextRandom(state) % 2U);
    const std::uint32_t size = BuildSegment(datagram, request_id, offset, more, length,
                                            static_cast<std::uint8_t>(round), aligned);
    const auto segment =
        MessageView::Parse(osp::span<const std::uint8_t>(datagram, size));
    REQUIRE(segment.has_value());

    MessageView out;
    const bool complete = reassembly.Feed(segment.value(), round, out);
    REQUIRE(reassembly.ActiveCount() <= kMaxSessions);
    if (complete) {
      REQUIRE(out.Body().size() <= kMaxPayload);
      REQUIRE(!out.Head().Type().IsTp());
    }
    if (0U == (NextRandom(state) % 16U)) {
      reassembly.Tick(round + (NextRandom(state) % 60U));
      REQUIRE(reassembly.ActiveCount() <= kMaxSessions);
    }
  }
}
