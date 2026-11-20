/**
 * @file message_id.hpp
 * @brief SOME/IP message ID: service ID [31:16] and method ID [15:0].
 */

#ifndef OSP_SOMEIP_MESSAGE_ID_HPP_
#define OSP_SOMEIP_MESSAGE_ID_HPP_

#include <cstdint>

namespace osp::someip {

/// @brief 32-bit identifier unique for each message in a vehicle network.
class MessageId final {
 public:
  constexpr MessageId() noexcept = default;

  explicit constexpr MessageId(std::uint32_t value) noexcept : value_(value) {}

  static constexpr MessageId FromServiceAndMethod(std::uint16_t service_id,
                                                  std::uint16_t method_id) noexcept {
    return MessageId((static_cast<std::uint32_t>(service_id) << 16U) |
                     static_cast<std::uint32_t>(method_id));
  }

  /// @brief Message ID of SOME/IP Service Discovery (0xFFFF8100).
  static constexpr MessageId Sd() noexcept { return MessageId(kSdValue); }

  constexpr std::uint32_t Value() const noexcept { return value_; }
  constexpr std::uint16_t ServiceId() const noexcept {
    return static_cast<std::uint16_t>(value_ >> 16U);
  }
  constexpr std::uint16_t MethodId() const noexcept {
    return static_cast<std::uint16_t>(value_ & 0xFFFFU);
  }

  /// @brief True when the method ID marks an event/notification (bit 15 set).
  constexpr bool IsEvent() const noexcept { return 0U != (MethodId() & 0x8000U); }

  constexpr bool IsSd() const noexcept { return kSdValue == value_; }

  constexpr void SetValue(std::uint32_t value) noexcept { value_ = value; }

  constexpr void SetServiceId(std::uint16_t service_id) noexcept {
    value_ = (value_ & 0x0000FFFFU) | (static_cast<std::uint32_t>(service_id) << 16U);
  }

  constexpr void SetMethodId(std::uint16_t method_id) noexcept {
    value_ = (value_ & 0xFFFF0000U) | static_cast<std::uint32_t>(method_id);
  }

  friend constexpr bool operator==(MessageId lhs, MessageId rhs) noexcept {
    return lhs.value_ == rhs.value_;
  }
  friend constexpr bool operator!=(MessageId lhs, MessageId rhs) noexcept {
    return !(lhs == rhs);
  }

 private:
  static constexpr std::uint32_t kSdValue = 0xFFFF8100U;

  std::uint32_t value_{0U};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_MESSAGE_ID_HPP_
