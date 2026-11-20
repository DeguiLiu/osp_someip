/**
 * @file message_type.hpp
 * @brief SOME/IP message type field: message type plus the TP flag.
 */

#ifndef OSP_SOMEIP_MESSAGE_TYPE_HPP_
#define OSP_SOMEIP_MESSAGE_TYPE_HPP_

#include "osp/someip/error.hpp"
#include "osp/vocabulary.hpp"

#include <cstdint>

namespace osp::someip {

/// @brief SOME/IP TP flag inside the message type byte.
inline constexpr std::uint8_t kMessageTypeTpFlag = 0x20U;

/// @brief Message types, valued at their on-wire base encoding.
enum class MessageType : std::uint8_t {
  kRequest = 0x00U,
  kRequestNoReturn = 0x01U,
  kNotification = 0x02U,
  kResponse = 0x80U,
  kError = 0x81U,
};

namespace detail {

constexpr std::uint8_t ClearTpFlag(std::uint8_t raw) noexcept {
  return static_cast<std::uint8_t>(raw & static_cast<std::uint8_t>(~kMessageTypeTpFlag));
}

constexpr bool IsKnownType(std::uint8_t base) noexcept {
  return base == 0x00U || base == 0x01U || base == 0x02U || base == 0x80U || base == 0x81U;
}

}  // namespace detail

/// @brief Validated message type byte; construction rejects unknown values.
class MessageTypeField final {
 public:
  constexpr MessageTypeField() noexcept = default;

  static constexpr MessageTypeField New(MessageType type, bool tp) noexcept {
    return MessageTypeField(static_cast<std::uint8_t>(type) |
                            (tp ? kMessageTypeTpFlag : static_cast<std::uint8_t>(0U)));
  }

  static constexpr MessageTypeField NewSd() noexcept {
    return New(MessageType::kNotification, false);
  }

  static osp::expected<MessageTypeField, Error> FromByte(std::uint8_t raw) noexcept {
    if (!detail::IsKnownType(detail::ClearTpFlag(raw))) {
      return osp::expected<MessageTypeField, Error>::error(Error::InvalidMessageTypeField(raw));
    }
    return osp::expected<MessageTypeField, Error>::success(MessageTypeField(raw));
  }

  /// @brief Build from a byte already validated by a parser.
  static constexpr MessageTypeField FromValidatedByte(std::uint8_t raw) noexcept {
    return MessageTypeField(raw);
  }

  constexpr MessageType Type() const noexcept {
    return static_cast<MessageType>(detail::ClearTpFlag(raw_));
  }

  constexpr bool IsTp() const noexcept { return 0U != (raw_ & kMessageTypeTpFlag); }

  constexpr std::uint8_t Value() const noexcept { return raw_; }

  friend constexpr bool operator==(MessageTypeField lhs, MessageTypeField rhs) noexcept {
    return lhs.raw_ == rhs.raw_;
  }
  friend constexpr bool operator!=(MessageTypeField lhs, MessageTypeField rhs) noexcept {
    return !(lhs == rhs);
  }

 private:
  explicit constexpr MessageTypeField(std::uint8_t raw) noexcept : raw_(raw) {}

  std::uint8_t raw_{0U};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_MESSAGE_TYPE_HPP_
