/**
 * @file error.hpp
 * @brief Error codes for the SOME/IP wire codec.
 *
 * Errors are plain values: no exceptions, no heap, no message strings. The
 * `value` field carries the offending wire value, or the number of bytes an
 * encode or decode operation needed; `available` carries the byte count that
 * was actually present when the error is a size mismatch.
 */

#ifndef OSP_SOMEIP_ERROR_HPP_
#define OSP_SOMEIP_ERROR_HPP_

#include <cstdint>

namespace osp::someip {

/// @brief Kinds of failure the SOME/IP codec reports.
enum class ErrorCode : std::uint8_t {
  kNone = 0U,
  /// Input ended before the value could be read.
  kIncomplete,
  /// Output slice was too small for the encoded bytes.
  kInsufficientBuffer,
  /// Bytes remained after a value that must consume the whole buffer.
  kTrailingBytes,
  /// Protocol version field is not 0x01.
  kInvalidProtocolVersion,
  /// Message type field holds an unrecognized value.
  kInvalidMessageTypeField,
  /// Return code field holds an unrecognized value.
  kInvalidReturnCode,
  /// SOME/IP length field is smaller than the 8-byte minimum.
  kInvalidLength,
  /// SD TTL exceeds the 24-bit maximum.
  kInvalidTtl,
  /// Service Discovery payload violates an SD constraint.
  kInvalidMessage,
  /// A caller asked for an SD header on a message that is not SD.
  kNotSdMessage,
  /// Payload type does not serve the requested message ID.
  kUnsupportedMessageId,
};

/// @brief Value-type error returned by every codec entry point.
struct Error final {
  ErrorCode code{ErrorCode::kNone};
  std::uint32_t value{0U};
  std::uint32_t available{0U};

  constexpr bool operator==(const Error& rhs) const noexcept {
    return code == rhs.code && value == rhs.value && available == rhs.available;
  }
  constexpr bool operator!=(const Error& rhs) const noexcept { return !(*this == rhs); }

  static constexpr Error None() noexcept { return Error{ErrorCode::kNone, 0U, 0U}; }

  static constexpr Error Incomplete(std::uint32_t needed, std::uint32_t available) noexcept {
    return Error{ErrorCode::kIncomplete, needed, available};
  }

  static constexpr Error InsufficientBuffer(std::uint32_t needed,
                                            std::uint32_t available) noexcept {
    return Error{ErrorCode::kInsufficientBuffer, needed, available};
  }

  static constexpr Error TrailingBytes(std::uint32_t left) noexcept {
    return Error{ErrorCode::kTrailingBytes, left, 0U};
  }

  static constexpr Error InvalidProtocolVersion(std::uint8_t version) noexcept {
    return Error{ErrorCode::kInvalidProtocolVersion, version, 0U};
  }

  static constexpr Error InvalidMessageTypeField(std::uint8_t field) noexcept {
    return Error{ErrorCode::kInvalidMessageTypeField, field, 0U};
  }

  static constexpr Error InvalidReturnCode(std::uint8_t code_value) noexcept {
    return Error{ErrorCode::kInvalidReturnCode, code_value, 0U};
  }

  static constexpr Error InvalidLength(std::uint32_t length) noexcept {
    return Error{ErrorCode::kInvalidLength, length, 0U};
  }

  static constexpr Error InvalidTtl(std::uint32_t ttl) noexcept {
    return Error{ErrorCode::kInvalidTtl, ttl, 0U};
  }

  static constexpr Error InvalidMessage(std::uint16_t reason) noexcept {
    return Error{ErrorCode::kInvalidMessage, reason, 0U};
  }

  static constexpr Error NotSdMessage() noexcept {
    return Error{ErrorCode::kNotSdMessage, 0U, 0U};
  }
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_ERROR_HPP_
