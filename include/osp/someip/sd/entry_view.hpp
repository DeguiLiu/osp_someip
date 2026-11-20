/**
 * @file entry_view.hpp
 * @brief Zero-copy view over one 16-byte SD entry.
 *
 * A parsed view trusts nothing about its buffer beyond the 16 bytes it was
 * built from: the type byte is validated once in Parse(), and the accessors
 * then read fixed offsets. ToEntry() rebuilds the owned value type so the
 * rest of the stack can work with plain values.
 */

#ifndef OSP_SOMEIP_SD_ENTRY_VIEW_HPP_
#define OSP_SOMEIP_SD_ENTRY_VIEW_HPP_

#include "osp/someip/error.hpp"
#include "osp/someip/sd/entry.hpp"
#include "osp/someip/sd/types.hpp"
#include "osp/someip/wire.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <cstddef>
#include <cstdint>

namespace osp::someip::sd {

/// @brief Validated zero-copy view over one SD entry.
class EntryView final {
 public:
  constexpr EntryView() noexcept = default;

  /// @brief Parse one entry; rejects a short buffer and an unknown type byte.
  static osp::expected<EntryView, Error> Parse(
      osp::span<const std::uint8_t> buf) noexcept {
    if (buf.size() < kEntrySize) {
      return osp::expected<EntryView, Error>::error(
          Error::Incomplete(kEntrySize, static_cast<std::uint32_t>(buf.size())));
    }
    const std::uint8_t type = buf[0U];
    if (!IsKnownType(type)) {
      return osp::expected<EntryView, Error>::error(Error::InvalidEntryType(type));
    }
    return osp::expected<EntryView, Error>::success(
        EntryView(osp::span<const std::uint8_t>(buf.data(), kEntrySize)));
  }

  /// @brief Wire type byte: 0x00 find, 0x01 offer, 0x06 subscribe, 0x07 ack.
  constexpr std::uint8_t WireType() const noexcept { return bytes_[0U]; }

  /// @brief Resolved kind, with TTL separating offer/stop and ack/nack.
  constexpr Entry::Kind Kind() const noexcept {
    switch (WireType()) {
      case 0x01U:
        return (0U == Ttl()) ? Entry::Kind::kStopOfferService : Entry::Kind::kOfferService;
      case 0x06U:
        return Entry::Kind::kSubscribe;
      case 0x07U:
        return (0U == Ttl()) ? Entry::Kind::kSubscribeNack : Entry::Kind::kSubscribeAck;
      case 0x00U:
      default:
        return Entry::Kind::kFindService;
    }
  }

  constexpr std::uint8_t IndexFirstOptionsRun() const noexcept { return bytes_[1U]; }
  constexpr std::uint8_t IndexSecondOptionsRun() const noexcept { return bytes_[2U]; }
  constexpr OptionsCount Count() const noexcept { return OptionsCount::FromByte(bytes_[3U]); }
  constexpr std::uint16_t ServiceId() const noexcept { return detail::ReadU16Be(bytes_, 4U); }
  constexpr std::uint16_t InstanceId() const noexcept { return detail::ReadU16Be(bytes_, 6U); }
  constexpr std::uint8_t MajorVersion() const noexcept { return bytes_[8U]; }
  constexpr std::uint32_t Ttl() const noexcept { return detail::ReadU24Be(bytes_, 9U); }
  constexpr std::uint32_t MinorVersion() const noexcept { return detail::ReadU32Be(bytes_, 12U); }
  /// @brief Counter field of an event group entry, masked to its 4 bits.
  constexpr std::uint16_t Counter() const noexcept {
    return static_cast<std::uint16_t>(detail::ReadU16Be(bytes_, 12U) & 0x000FU);
  }
  constexpr std::uint16_t EventGroupId() const noexcept {
    return detail::ReadU16Be(bytes_, 14U);
  }

  constexpr bool IsServiceKind() const noexcept {
    return Entry::Kind::kFindService == Kind() || Entry::Kind::kOfferService == Kind() ||
           Entry::Kind::kStopOfferService == Kind();
  }

  /// @brief Rebuild the owned entry with the same fields.
  Entry ToEntry() const noexcept {
    if (IsServiceKind()) {
      ServiceEntry service;
      service.options_count = Count();
      service.service_id = ServiceId();
      service.instance_id = InstanceId();
      service.major_version = MajorVersion();
      service.ttl = Ttl();
      service.minor_version = MinorVersion();
      service.index_first_options_run = IndexFirstOptionsRun();
      service.index_second_options_run = IndexSecondOptionsRun();
      switch (Kind()) {
        case Entry::Kind::kOfferService:
          return Entry::Offer(service);
        case Entry::Kind::kStopOfferService:
          return Entry::StopOffer(service);
        case Entry::Kind::kFindService:
        default:
          return Entry::Find(service);
      }
    }
    EventGroupEntry group;
    group.options_count = Count();
    group.service_id = ServiceId();
    group.instance_id = InstanceId();
    group.major_version = MajorVersion();
    group.ttl = Ttl();
    group.counter = Counter();
    group.event_group_id = EventGroupId();
    group.index_first_options_run = IndexFirstOptionsRun();
    group.index_second_options_run = IndexSecondOptionsRun();
    switch (Kind()) {
      case Entry::Kind::kSubscribe:
        return Entry::Subscribe(group);
      case Entry::Kind::kSubscribeNack:
        return Entry::Nack(group);
      case Entry::Kind::kSubscribeAck:
      default:
        return Entry::Ack(group);
    }
  }

 private:
  static constexpr bool IsKnownType(std::uint8_t type) noexcept {
    return 0x00U == type || 0x01U == type || 0x06U == type || 0x07U == type;
  }

  constexpr explicit EntryView(osp::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}

  osp::span<const std::uint8_t> bytes_{};
};

}  // namespace osp::someip::sd

#endif  // OSP_SOMEIP_SD_ENTRY_VIEW_HPP_
