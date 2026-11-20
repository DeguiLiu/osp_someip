/**
 * @file return_code.hpp
 * @brief SOME/IP return code: named codes plus the generic and
 *        interface-specific ranges.
 */

#ifndef OSP_SOMEIP_RETURN_CODE_HPP_
#define OSP_SOMEIP_RETURN_CODE_HPP_

#include "osp/someip/error.hpp"
#include "osp/vocabulary.hpp"

#include <cstdint>

namespace osp::someip {

/// @brief Which range of the return code space a value belongs to.
enum class ReturnCodeKind : std::uint8_t {
  kNamed = 0U,
  /// 0x0B..=0x1F, generic errors defined by the protocol.
  kGeneric,
  /// 0x20..=0x5E, errors defined by the service interface.
  kInterface,
};

/// @brief Validated SOME/IP return code.
class ReturnCode final {
 public:
  constexpr ReturnCode() noexcept = default;

  static constexpr ReturnCode Ok() noexcept { return Named(0x00U); }
  static constexpr ReturnCode NotOk() noexcept { return Named(0x01U); }
  static constexpr ReturnCode UnknownService() noexcept { return Named(0x02U); }
  static constexpr ReturnCode UnknownMethod() noexcept { return Named(0x03U); }
  static constexpr ReturnCode NotReady() noexcept { return Named(0x04U); }
  static constexpr ReturnCode NotReachable() noexcept { return Named(0x05U); }
  static constexpr ReturnCode Timeout() noexcept { return Named(0x06U); }
  static constexpr ReturnCode WrongProtocolVersion() noexcept { return Named(0x07U); }
  static constexpr ReturnCode WrongInterfaceVersion() noexcept { return Named(0x08U); }
  static constexpr ReturnCode MalformedMessage() noexcept { return Named(0x09U); }
  static constexpr ReturnCode WrongMessageType() noexcept { return Named(0x0AU); }

  static constexpr ReturnCode Generic(std::uint8_t value) noexcept {
    return ReturnCode(value, ReturnCodeKind::kGeneric);
  }

  static constexpr ReturnCode Interface(std::uint8_t value) noexcept {
    return ReturnCode(value, ReturnCodeKind::kInterface);
  }

  /// @brief Decode a wire byte, rejecting 0x5F..=0xFF.
  static osp::expected<ReturnCode, Error> FromByte(std::uint8_t value) noexcept {
    if (value <= 0x0AU) {
      return osp::expected<ReturnCode, Error>::success(Named(value));
    }
    if (value >= 0x0BU && value <= 0x1FU) {
      return osp::expected<ReturnCode, Error>::success(Generic(value));
    }
    if (value >= 0x20U && value <= 0x5EU) {
      return osp::expected<ReturnCode, Error>::success(Interface(value));
    }
    return osp::expected<ReturnCode, Error>::error(Error::InvalidReturnCode(value));
  }

  /// @brief Build from a byte a parser has already validated.
  static constexpr ReturnCode FromValidatedByte(std::uint8_t value) noexcept {
    return (value <= 0x0AU) ? Named(value)
                            : ((value <= 0x1FU) ? Generic(value) : Interface(value));
  }

  constexpr std::uint8_t Value() const noexcept { return value_; }
  constexpr ReturnCodeKind Kind() const noexcept { return kind_; }

  friend constexpr bool operator==(ReturnCode lhs, ReturnCode rhs) noexcept {
    return lhs.value_ == rhs.value_ && lhs.kind_ == rhs.kind_;
  }
  friend constexpr bool operator!=(ReturnCode lhs, ReturnCode rhs) noexcept {
    return !(lhs == rhs);
  }

 private:
  static constexpr ReturnCode Named(std::uint8_t value) noexcept {
    return ReturnCode(value, ReturnCodeKind::kNamed);
  }

  constexpr ReturnCode(std::uint8_t value, ReturnCodeKind kind) noexcept
      : value_(value), kind_(kind) {}

  std::uint8_t value_{0U};
  ReturnCodeKind kind_{ReturnCodeKind::kNamed};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_RETURN_CODE_HPP_
