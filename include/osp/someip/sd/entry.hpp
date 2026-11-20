/**
 * @file entry.hpp
 * @brief SOME/IP-SD entry value types and their fixed 16-byte wire encoding.
 *
 * An entry is a fixed-size record: a type byte, two option-run indices, the
 * packed option counts, the service/instance/version identity, a 24-bit TTL
 * and a trailing field that depends on the entry type (minor version for
 * service entries, counter plus event group ID for event group entries).
 *
 * Two of the six kinds are not distinct types on the wire: a stop-offer is an
 * offer (0x01) with a TTL of 0, and a subscribe-nack is a subscribe-ack (0x07)
 * with a TTL of 0. Encoding always emits exactly 16 bytes and never
 * allocates, throws, or reads past the entry.
 */

#ifndef OSP_SOMEIP_SD_ENTRY_HPP_
#define OSP_SOMEIP_SD_ENTRY_HPP_

#include "osp/someip/error.hpp"
#include "osp/someip/sd/types.hpp"
#include "osp/someip/wire.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <cstdint>
#include <type_traits>

namespace osp::someip::sd {

/**
 * @brief Payload of a find, offer or stop-offer entry.
 *
 * The last four bytes of a service entry carry the minor version, so this
 * struct covers all three service kinds.
 */
struct ServiceEntry final {
  OptionsCount options_count{};
  std::uint16_t service_id{0U};
  std::uint16_t instance_id{0U};
  std::uint8_t major_version{0U};
  /// @brief Seconds the entry stays valid, at most kMaxTtl.
  std::uint32_t ttl{0U};
  std::uint32_t minor_version{0U};
  /// @brief Index of the first options run in the SD header's option array.
  std::uint8_t index_first_options_run{0U};
  /// @brief Index of the second options run, when the entry uses two runs.
  std::uint8_t index_second_options_run{0U};

  /**
   * @brief Find entry with the wildcard any-instance, any-version fields.
   *
   * Instance 0xFFFF, major 0xFF, TTL 0x00FFFFFF and minor 0xFFFFFFFF match
   * every provider, which is the form vsomeip and other stacks expect from a
   * service search.
   */
  static constexpr ServiceEntry Find(std::uint16_t service_id) noexcept {
    return ServiceEntry{OptionsCount(1U, 0U), service_id, 0xFFFFU, 0xFFU, kMaxTtl,
                        0xFFFFFFFFU};
  }
};

/// @brief Payload of a subscribe, subscribe-ack or subscribe-nack entry.
struct EventGroupEntry final {
  OptionsCount options_count{};
  std::uint16_t service_id{0U};
  std::uint16_t instance_id{0U};
  std::uint8_t major_version{0U};
  /// @brief Subscription lifetime in seconds; overridden by the entry kind.
  std::uint32_t ttl{0U};
  std::uint16_t counter{0U};
  std::uint16_t event_group_id{0U};
  /// @brief Index of the first options run in the SD header's option array.
  std::uint8_t index_first_options_run{0U};
  /// @brief Index of the second options run, when the entry uses two runs.
  std::uint8_t index_second_options_run{0U};
};

static_assert(std::is_trivially_copyable<ServiceEntry>::value,
              "ServiceEntry crosses the wire/transport boundary by value");
static_assert(std::is_trivially_copyable<EventGroupEntry>::value,
              "EventGroupEntry crosses the wire/transport boundary by value");

/**
 * @brief One SD entry, encodable in exactly 16 bytes.
 *
 * The kind fixes the type byte and the TTL of the stop-offer (0) and
 * subscribe-nack (0) kinds; a subscribe-ack carries the TTL the publisher
 * granted. A service kind reads the identity from Service();
 * an event group kind reads it from EventGroup().
 */
class Entry final : public osp::someip::WireEncoder<Entry> {
 public:
  /// @brief Which of the six SD operations this entry expresses.
  enum class Kind : std::uint8_t {
    kFindService = 0U,
    kOfferService,
    kStopOfferService,
    kSubscribe,
    kSubscribeAck,
    kSubscribeNack,
  };

  /// @brief Find service entry (type 0x00).
  static Entry Find(ServiceEntry entry) noexcept {
    return Entry(Kind::kFindService, entry, EventGroupEntry{});
  }

  /// @brief Offer service entry (type 0x01, caller TTL).
  static Entry Offer(ServiceEntry entry) noexcept {
    return Entry(Kind::kOfferService, entry, EventGroupEntry{});
  }

  /// @brief Stop offer (type 0x01, TTL forced to 0).
  static Entry StopOffer(ServiceEntry entry) noexcept {
    return Entry(Kind::kStopOfferService, entry, EventGroupEntry{});
  }

  /// @brief Subscribe event group entry (type 0x06, caller TTL).
  static Entry Subscribe(EventGroupEntry entry) noexcept {
    return Entry(Kind::kSubscribe, ServiceEntry{}, entry);
  }

  /// @brief Subscribe acknowledgement (type 0x07, caller TTL = granted lifetime).
  static Entry Ack(EventGroupEntry entry) noexcept {
    return Entry(Kind::kSubscribeAck, ServiceEntry{}, entry);
  }

  /// @brief Subscribe rejection (type 0x07, TTL forced to 0).
  static Entry Nack(EventGroupEntry entry) noexcept {
    return Entry(Kind::kSubscribeNack, ServiceEntry{}, entry);
  }

  constexpr Kind Type() const noexcept { return kind_; }

  /// @brief True for find, offer and stop-offer; false for event group kinds.
  constexpr bool IsService() const noexcept {
    return Kind::kFindService == kind_ || Kind::kOfferService == kind_ ||
           Kind::kStopOfferService == kind_;
  }

  /// @brief Service payload; only meaningful when IsService() is true.
  constexpr const ServiceEntry& Service() const noexcept { return service_; }

  /// @brief Event group payload; only meaningful when IsService() is false.
  constexpr const EventGroupEntry& EventGroup() const noexcept { return event_group_; }

  constexpr std::uint32_t DoEncodedSize() const noexcept { return kEntrySize; }

  osp::expected<std::uint32_t, Error> DoEncodeTo(
      osp::span<std::uint8_t> out) const noexcept {
    if (out.size() < kEntrySize) {
      return osp::expected<std::uint32_t, Error>::error(
          Error::InsufficientBuffer(kEntrySize, static_cast<std::uint32_t>(out.size())));
    }
    ByteWriter writer(out);
    // Bytes 1 and 2 are the first and second option run indices; both point
    // at the head of the option array, which is the only layout the builders
    // emit for a single run.
    const bool ok = writer.WriteU8(static_cast<std::uint8_t>(WireType())) &&
                    writer.WriteU8(IndexFirst()) && writer.WriteU8(IndexSecond()) &&
                    writer.WriteU8(OptionsCountValue().Value()) &&
                    writer.WriteU16Be(ServiceId()) && writer.WriteU16Be(InstanceId()) &&
                    writer.WriteU8(MajorVersion()) && writer.WriteU24Be(EffectiveTtl()) &&
                    WriteTrailer(writer);
    if (!ok) {
      return osp::expected<std::uint32_t, Error>::error(
          Error::InsufficientBuffer(kEntrySize, static_cast<std::uint32_t>(out.size())));
    }
    return osp::expected<std::uint32_t, Error>::success(
        static_cast<std::uint32_t>(writer.Position()));
  }

 private:
  constexpr Entry(Kind kind, const ServiceEntry& service,
                  const EventGroupEntry& event_group) noexcept
      : kind_(kind), service_(service), event_group_(event_group) {}

  constexpr EntryType WireType() const noexcept {
    switch (kind_) {
      case Kind::kOfferService:
      case Kind::kStopOfferService:
        return EntryType::kOfferService;
      case Kind::kSubscribe:
        return EntryType::kSubscribe;
      case Kind::kSubscribeAck:
      case Kind::kSubscribeNack:
        return EntryType::kSubscribeAck;
      case Kind::kFindService:
      default:
        return EntryType::kFindService;
    }
  }

  constexpr std::uint32_t EffectiveTtl() const noexcept {
    switch (kind_) {
      case Kind::kStopOfferService:
      case Kind::kSubscribeNack:
        return 0U;
      case Kind::kSubscribeAck:
        return event_group_.ttl;
      case Kind::kSubscribe:
        return event_group_.ttl;
      case Kind::kFindService:
      case Kind::kOfferService:
      default:
        return service_.ttl;
    }
  }

  constexpr OptionsCount OptionsCountValue() const noexcept {
    return IsService() ? service_.options_count : event_group_.options_count;
  }

  constexpr std::uint8_t IndexFirst() const noexcept {
    return IsService() ? service_.index_first_options_run
                       : event_group_.index_first_options_run;
  }

  constexpr std::uint8_t IndexSecond() const noexcept {
    return IsService() ? service_.index_second_options_run
                       : event_group_.index_second_options_run;
  }

  constexpr std::uint16_t ServiceId() const noexcept {
    return IsService() ? service_.service_id : event_group_.service_id;
  }

  constexpr std::uint16_t InstanceId() const noexcept {
    return IsService() ? service_.instance_id : event_group_.instance_id;
  }

  constexpr std::uint8_t MajorVersion() const noexcept {
    return IsService() ? service_.major_version : event_group_.major_version;
  }

  bool WriteTrailer(ByteWriter& writer) const noexcept {
    if (IsService()) {
      return writer.WriteU32Be(service_.minor_version);
    }
    return writer.WriteU16Be(event_group_.counter) &&
           writer.WriteU16Be(event_group_.event_group_id);
  }

  Kind kind_{Kind::kFindService};
  ServiceEntry service_{};
  EventGroupEntry event_group_{};
};

static_assert(std::is_trivially_copyable<Entry>::value,
              "Entry is a value type passed between builders and transports");

}  // namespace osp::someip::sd

#endif  // OSP_SOMEIP_SD_ENTRY_HPP_
