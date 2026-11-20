/**
 * @file tp.hpp
 * @brief SOME/IP-TP segment header and bounded reassembly.
 *
 * A TP segment carries a four-byte header at the start of its payload: a
 * 28-bit offset in 16-byte units, three reserved bits, and the more-segments
 * flag. Reassembly stores segments in a fixed buffer indexed by
 * (message ID, request ID), tracks which 16-byte units have arrived so the
 * segments may come in any order, and drops an assembly after a timeout or on
 * overlapping segments. Nothing allocates.
 */

#ifndef OSP_SOMEIP_TP_HPP_
#define OSP_SOMEIP_TP_HPP_

#include "osp/someip/error.hpp"
#include "osp/someip/header.hpp"
#include "osp/someip/message.hpp"
#include "osp/someip/message_id.hpp"
#include "osp/someip/message_type.hpp"
#include "osp/someip/return_code.hpp"
#include "osp/someip/wire.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace osp::someip {

/// @brief The four-byte SOME/IP-TP header at the start of a TP payload.
struct TpHeader final {
  static constexpr std::uint32_t kSize = 4U;
  static constexpr std::uint32_t kOffsetUnit = 16U;
  static constexpr std::uint32_t kOffsetMask = 0xFFFFFFF0U;

  /// @brief Byte offset of this segment inside the reassembled payload.
  std::uint32_t offset{0U};
  /// @brief True when more segments follow.
  bool more{false};

  static osp::expected<TpHeader, Error> Parse(
      osp::span<const std::uint8_t> payload) noexcept {
    if (payload.size() < kSize) {
      return osp::expected<TpHeader, Error>::error(
          Error::Incomplete(kSize, static_cast<std::uint32_t>(payload.size())));
    }
    const std::uint32_t word = detail::ReadU32Be(payload, 0U);
    if (0U != (word & 0x0EU)) {
      return osp::expected<TpHeader, Error>::error(Error::InvalidTpSegment(word));
    }
    TpHeader header;
    header.offset = ((word & kOffsetMask) >> 4U) * kOffsetUnit;
    header.more = (0U != (word & 1U));
    return osp::expected<TpHeader, Error>::success(header);
  }

  /// @brief Write a TP header; @p offset must be a multiple of kOffsetUnit.
  static bool Write(osp::span<std::uint8_t> out, std::uint32_t offset,
                    bool more) noexcept {
    if (out.size() < kSize || 0U != (offset % kOffsetUnit)) {
      return false;
    }
    const std::uint32_t units = offset / kOffsetUnit;
    if (units > (kOffsetMask >> 4U)) {
      return false;
    }
    ByteWriter writer(out);
    return writer.WriteU32Be((units << 4U) | (more ? 1U : 0U));
  }
};

/// @brief Reassembly policy that ignores TP segments; costs no memory.
class NoTpReassembly final {
 public:
  bool Feed(const MessageView& /*segment*/, std::uint32_t /*now_ms*/,
            MessageView& /*out*/) noexcept {
    return false;
  }
  void Tick(std::uint32_t /*now_ms*/) noexcept {}
  std::uint32_t ActiveCount() const noexcept { return 0U; }
};

/**
 * @brief Bounded, single-threaded TP reassembler.
 *
 * @tparam MaxPayload   Largest reassembled payload, in bytes.
 * @tparam MaxSessions  Concurrent assemblies; the footprint is
 *                      MaxSessions * (MaxPayload + 16) bytes.
 * @tparam TimeoutMs    An assembly that sees no segment for this long is dropped.
 *
 * Feed() writes the completed message into the slot buffer and returns a view
 * over it. The view stays valid until the next Feed() or Tick() on the same
 * slot.
 */
template <std::uint32_t MaxPayload = 4096U, std::uint32_t MaxSessions = 1U,
          std::uint32_t TimeoutMs = 1000U>
class TpReassembly final {
 public:
  static_assert(MaxSessions > 0U, "at least one session slot is required");
  static_assert(MaxPayload >= TpHeader::kOffsetUnit, "payload too small for one segment");

  /// @brief Feed one TP segment; true when it completed an assembly.
  bool Feed(const MessageView& segment, std::uint32_t now_ms,
            MessageView& out) noexcept {
    const HeaderView head = segment.Head();
    if (!head.Type().IsTp() || head.IsSd()) {
      return false;
    }
    const auto parsed = TpHeader::Parse(segment.Body());
    if (!parsed.has_value()) {
      return false;
    }
    const TpHeader tp = parsed.value();
    const osp::span<const std::uint8_t> body = segment.Body();
    const osp::span<const std::uint8_t> data(
        body.data() + TpHeader::kSize, body.size() - TpHeader::kSize);
    if (data.empty() || tp.offset > MaxPayload || data.size() > (MaxPayload - tp.offset)) {
      Drop(head.Id(), head.RequestId());
      return false;
    }
    // A later segment may arrive before the first one, so any segment can start
    // an assembly. A segment that overlaps what is already stored is a
    // retransmission when it starts at offset 0 and a contradiction otherwise.
    std::int32_t index = Find(head.Id(), head.RequestId());
    if (index < 0) {
      index = Begin(head, now_ms);
    } else if (Overlaps(slots_[static_cast<std::size_t>(index)], tp.offset,
                        static_cast<std::uint32_t>(data.size()))) {
      if (0U != tp.offset) {
        slots_[static_cast<std::size_t>(index)].active = false;
        return false;
      }
      index = Begin(head, now_ms);
    }
    Slot& slot = slots_[static_cast<std::size_t>(index)];
    const std::size_t base = static_cast<std::size_t>(kHeaderSize) + tp.offset;
    std::memcpy(buffers_[static_cast<std::size_t>(index)].data() + base, data.data(), data.size());
    MarkUnits(slot, tp.offset, static_cast<std::uint32_t>(data.size()));
    if (!tp.more) {
      slot.total = tp.offset + static_cast<std::uint32_t>(data.size());
    }
    slot.last_ms = now_ms;
    if (0U == slot.total || !Complete(slot, slot.total)) {
      return false;
    }
    return Finish(index, out);
  }

  /// @brief Drop assemblies that have not seen a segment within TimeoutMs.
  void Tick(std::uint32_t now_ms) noexcept {
    for (auto& slot : slots_) {
      if (slot.active && (now_ms - slot.last_ms) >= TimeoutMs) {
        slot.active = false;
      }
    }
  }

  std::uint32_t ActiveCount() const noexcept {
    std::uint32_t count = 0U;
    for (const auto& slot : slots_) {
      if (slot.active) {
        ++count;
      }
    }
    return count;
  }

 private:
  struct Slot final {
    bool active{false};
    MessageId id{};
    std::uint32_t request_id{0U};
    std::uint8_t protocol_version{0x01U};
    std::uint8_t interface_version{0U};
    ReturnCode code{ReturnCode::Ok()};
    MessageType type{MessageType::kRequest};
    /// @brief End of the message, known once the last segment arrives.
    std::uint32_t total{0U};
    std::uint32_t last_ms{0U};
    std::array<std::uint8_t, (MaxPayload / TpHeader::kOffsetUnit + 7U) / 8U> covered{};
  };

  using Buffer = std::array<std::uint8_t, static_cast<std::size_t>(MaxPayload) + kHeaderSize>;

  std::int32_t Begin(const HeaderView& head, std::uint32_t now_ms) noexcept {
    std::int32_t free_index = -1;
    std::int32_t oldest_index = -1;
    for (std::uint32_t index = 0U; index < MaxSessions; ++index) {
      const Slot& slot = slots_[index];
      const auto position = static_cast<std::int32_t>(index);
      if (!slot.active) {
        if (free_index < 0) {
          free_index = position;
        }
        continue;
      }
      if (oldest_index < 0 ||
          slot.last_ms < slots_[static_cast<std::size_t>(oldest_index)].last_ms) {
        oldest_index = position;
      }
    }
    const std::int32_t chosen =
        (free_index >= 0) ? free_index : ((oldest_index >= 0) ? oldest_index : 0);
    Slot& slot = slots_[static_cast<std::size_t>(chosen)];
    slot.active = true;
    slot.id = head.Id();
    slot.request_id = head.RequestId();
    slot.protocol_version = head.ProtocolVersion();
    slot.interface_version = head.InterfaceVersion();
    slot.code = head.Code();
    slot.type = head.Type().Type();
    slot.total = 0U;
    slot.last_ms = now_ms;
    slot.covered.fill(0U);
    return chosen;
  }

  std::int32_t Find(MessageId id, std::uint32_t request_id) noexcept {
    for (std::uint32_t index = 0U; index < MaxSessions; ++index) {
      const Slot& slot = slots_[index];
      if (slot.active && slot.id == id && slot.request_id == request_id) {
        return static_cast<std::int32_t>(index);
      }
    }
    return -1;
  }

  void Drop(MessageId id, std::uint32_t request_id) noexcept {
    const std::int32_t index = Find(id, request_id);
    if (index >= 0) {
      slots_[static_cast<std::size_t>(index)].active = false;
    }
  }

  static bool UnitBit(const Slot& slot, std::uint32_t unit) noexcept {
    return 0U != (slot.covered[unit / 8U] & static_cast<std::uint8_t>(1U << (unit % 8U)));
  }

  static void MarkUnits(Slot& slot, std::uint32_t offset, std::uint32_t length) noexcept {
    const std::uint32_t first = offset / TpHeader::kOffsetUnit;
    const std::uint32_t last = (offset + length + TpHeader::kOffsetUnit - 1U) / TpHeader::kOffsetUnit;
    for (std::uint32_t unit = first; unit < last; ++unit) {
      slot.covered[unit / 8U] =
          static_cast<std::uint8_t>(slot.covered[unit / 8U] | (1U << (unit % 8U)));
    }
  }

  /// @brief True when any 16-byte unit of the new segment is already filled.
  static bool Overlaps(const Slot& slot, std::uint32_t offset, std::uint32_t length) noexcept {
    const std::uint32_t first = offset / TpHeader::kOffsetUnit;
    const std::uint32_t last = (offset + length + TpHeader::kOffsetUnit - 1U) / TpHeader::kOffsetUnit;
    for (std::uint32_t unit = first; unit < last; ++unit) {
      if (UnitBit(slot, unit)) {
        return true;
      }
    }
    return false;
  }

  /// @brief True when every unit up to @p total has arrived.
  static bool Complete(const Slot& slot, std::uint32_t total) noexcept {
    const std::uint32_t units = (total + TpHeader::kOffsetUnit - 1U) / TpHeader::kOffsetUnit;
    for (std::uint32_t unit = 0U; unit < units; ++unit) {
      if (!UnitBit(slot, unit)) {
        return false;
      }
    }
    return true;
  }

  bool Finish(std::int32_t index, MessageView& out) noexcept {
    Slot& slot = slots_[static_cast<std::size_t>(index)];
    Buffer& buffer = buffers_[static_cast<std::size_t>(index)];
    const std::uint32_t total = slot.total;
    const Header header = Header::New(
        slot.id, slot.request_id, slot.protocol_version, slot.interface_version,
        MessageTypeField::New(slot.type, false), slot.code, total);
    const auto written =
        header.EncodeTo(osp::make_span(buffer.data(), static_cast<std::size_t>(kHeaderSize)));
    slot.active = false;
    if (!written.has_value()) {
      return false;
    }
    const auto view = MessageView::Parse(
        osp::span<const std::uint8_t>(buffer.data(),
                                      static_cast<std::size_t>(kHeaderSize) + total));
    if (!view.has_value()) {
      return false;
    }
    out = view.value();
    return true;
  }

  std::array<Slot, MaxSessions> slots_{};
  std::array<Buffer, MaxSessions> buffers_{};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_TP_HPP_
