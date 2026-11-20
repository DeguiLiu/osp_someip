/**
 * @file types.hpp
 * @brief Shared SOME/IP-SD constants and value types.
 *
 * The entry, option, and header modules all include this file; it is the only
 * place the wire numbers live so no module can drift on a constant.
 */

#ifndef OSP_SOMEIP_SD_TYPES_HPP_
#define OSP_SOMEIP_SD_TYPES_HPP_

#include <cstdint>

namespace osp::someip::sd {

/// @brief On-wire size of one SD entry, in bytes.
inline constexpr std::uint32_t kEntrySize = 16U;

/// @brief Largest 24-bit SD TTL: valid until the offering node reboots.
inline constexpr std::uint32_t kMaxTtl = 0x00FFFFFFU;

/// @brief Largest configuration string an SD Configuration option carries.
inline constexpr std::uint32_t kMaxConfigurationStringLength = 256U;

/// @brief SD entry type byte.
enum class EntryType : std::uint8_t {
  kFindService = 0x00U,
  /// Offer, or stop-offer when the TTL is 0.
  kOfferService = 0x01U,
  kSubscribe = 0x06U,
  kSubscribeAck = 0x07U,
};

/// @brief SD option type byte.
enum class OptionType : std::uint8_t {
  kConfiguration = 0x01U,
  kLoadBalancing = 0x02U,
  kIpV4Endpoint = 0x04U,
  kIpV6Endpoint = 0x06U,
  kIpV4Multicast = 0x14U,
  kIpV6Multicast = 0x16U,
  kIpV4Sd = 0x24U,
  kIpV6Sd = 0x26U,
};

/// @brief Transport protocol byte of the endpoint and multicast options.
enum class TransportProtocol : std::uint8_t {
  kUdp = 0x11U,
  kTcp = 0x06U,
};

/// @brief Reboot state of the SD sender.
enum class RebootFlag : std::uint8_t {
  kContinuous = 0U,
  kRecentlyRebooted = 1U,
};

/// @brief SD flags byte; only the two high bits are used.
class Flags final {
 public:
  constexpr Flags() noexcept = default;

  static constexpr Flags New(bool reboot, bool unicast) noexcept {
    return Flags(reboot, unicast);
  }

  static constexpr Flags NewSd(RebootFlag reboot) noexcept {
    return Flags(RebootFlag::kRecentlyRebooted == reboot, true);
  }

  static constexpr Flags FromByte(std::uint8_t value) noexcept {
    return Flags(0U != (value & 0x80U), 0U != (value & 0x40U));
  }

  constexpr std::uint8_t Value() const noexcept {
    return static_cast<std::uint8_t>((reboot_ ? 0x80U : 0x00U) | (unicast_ ? 0x40U : 0x00U));
  }

  constexpr RebootFlag Reboot() const noexcept {
    return reboot_ ? RebootFlag::kRecentlyRebooted : RebootFlag::kContinuous;
  }

  constexpr bool Unicast() const noexcept { return unicast_; }

  friend constexpr bool operator==(Flags lhs, Flags rhs) noexcept {
    return lhs.reboot_ == rhs.reboot_ && lhs.unicast_ == rhs.unicast_;
  }
  friend constexpr bool operator!=(Flags lhs, Flags rhs) noexcept { return !(lhs == rhs); }

 private:
  constexpr Flags(bool reboot, bool unicast) noexcept : reboot_(reboot), unicast_(unicast) {}

  bool reboot_{false};
  bool unicast_{true};
};

/// @brief Packed pair of 4-bit option run counts.
class OptionsCount final {
 public:
  constexpr OptionsCount() noexcept = default;

  constexpr OptionsCount(std::uint8_t first, std::uint8_t second) noexcept
      : first_(first), second_(second) {}

  static constexpr OptionsCount FromByte(std::uint8_t value) noexcept {
    return OptionsCount(static_cast<std::uint8_t>((value & 0xF0U) >> 4U),
                        static_cast<std::uint8_t>(value & 0x0FU));
  }

  constexpr std::uint8_t Value() const noexcept {
    return static_cast<std::uint8_t>((static_cast<std::uint8_t>(first_ << 4U) & 0xF0U) |
                                     (second_ & 0x0FU));
  }

  constexpr std::uint8_t First() const noexcept { return first_; }
  constexpr std::uint8_t Second() const noexcept { return second_; }

  friend constexpr bool operator==(OptionsCount lhs, OptionsCount rhs) noexcept {
    return lhs.first_ == rhs.first_ && lhs.second_ == rhs.second_;
  }
  friend constexpr bool operator!=(OptionsCount lhs, OptionsCount rhs) noexcept {
    return !(lhs == rhs);
  }

 private:
  std::uint8_t first_{0U};
  std::uint8_t second_{0U};
};

}  // namespace osp::someip::sd

#endif  // OSP_SOMEIP_SD_TYPES_HPP_
