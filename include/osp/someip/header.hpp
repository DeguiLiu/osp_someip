/**
 * @file header.hpp
 * @brief SOME/IP 16-byte header: owned value and zero-copy view.
 */

#ifndef OSP_SOMEIP_HEADER_HPP_
#define OSP_SOMEIP_HEADER_HPP_

#include "osp/platform.hpp"
#include "osp/someip/error.hpp"
#include "osp/someip/message_id.hpp"
#include "osp/someip/message_type.hpp"
#include "osp/someip/return_code.hpp"
#include "osp/someip/wire.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace osp::someip {

/// @brief On-wire size of a SOME/IP header, in bytes.
inline constexpr std::uint32_t kHeaderSize = 16U;

class HeaderView;
struct ParsedHeader;

/// @brief Owned, encodable SOME/IP header.
class Header final : public WireEncoder<Header> {
 public:
  /// @brief Build from fields; `length` is computed as payload_len + 8.
  static Header New(MessageId message_id, std::uint32_t request_id,
                    std::uint8_t protocol_version, std::uint8_t interface_version,
                    MessageTypeField message_type, ReturnCode return_code,
                    std::uint32_t payload_len) noexcept {
    OSP_ASSERT(payload_len <= (UINT32_MAX - 8U));
    return Header(message_id, 8U + payload_len, request_id, protocol_version, interface_version,
                  message_type, return_code);
  }

  /// @brief Standard SOME/IP-SD header.
  static Header NewSd(std::uint32_t request_id, std::uint32_t sd_header_size) noexcept {
    OSP_ASSERT(sd_header_size <= (UINT32_MAX - 8U));
    return Header(MessageId::Sd(), 8U + sd_header_size, request_id, 0x01U, 0x01U,
                  MessageTypeField::NewSd(), ReturnCode::Ok());
  }

  /// @brief Event/notification header with an implicit Ok return code.
  static Header NewEvent(std::uint16_t service_id, std::uint16_t event_id,
                         std::uint32_t request_id, std::uint8_t protocol_version,
                         std::uint8_t interface_version, std::uint32_t payload_len) noexcept {
    OSP_ASSERT(payload_len <= (UINT32_MAX - 8U));
    return Header(MessageId::FromServiceAndMethod(service_id, event_id), 8U + payload_len,
                  request_id, protocol_version, interface_version,
                  MessageTypeField::New(MessageType::kNotification, false), ReturnCode::Ok());
  }

  /// @brief Build from raw on-wire fields (length taken as given).
  static constexpr Header FromFields(MessageId message_id, std::uint32_t length,
                                     std::uint32_t request_id, std::uint8_t protocol_version,
                                     std::uint8_t interface_version,
                                     MessageTypeField message_type,
                                     ReturnCode return_code) noexcept {
    return Header(message_id, length, request_id, protocol_version, interface_version,
                  message_type, return_code);
  }

  constexpr MessageId Id() const noexcept { return message_id_; }
  constexpr std::uint32_t Length() const noexcept { return length_; }
  constexpr std::uint32_t RequestId() const noexcept { return request_id_; }
  constexpr std::uint8_t ProtocolVersion() const noexcept { return protocol_version_; }
  constexpr std::uint8_t InterfaceVersion() const noexcept { return interface_version_; }
  constexpr MessageTypeField Type() const noexcept { return message_type_; }
  constexpr ReturnCode Code() const noexcept { return return_code_; }
  constexpr bool IsSd() const noexcept { return message_id_.IsSd(); }

  constexpr std::uint32_t PayloadSize() const noexcept {
    return (length_ > 8U) ? (length_ - 8U) : 0U;
  }

  constexpr void SetRequestId(std::uint32_t request_id) noexcept { request_id_ = request_id; }

  friend constexpr bool operator==(const Header& lhs, const Header& rhs) noexcept {
    return lhs.message_id_ == rhs.message_id_ && lhs.length_ == rhs.length_ &&
           lhs.request_id_ == rhs.request_id_ &&
           lhs.protocol_version_ == rhs.protocol_version_ &&
           lhs.interface_version_ == rhs.interface_version_ &&
           lhs.message_type_ == rhs.message_type_ && lhs.return_code_ == rhs.return_code_;
  }
  friend constexpr bool operator!=(const Header& lhs, const Header& rhs) noexcept {
    return !(lhs == rhs);
  }

  /// @brief Bytes 8..16, the part an E2E profile may protect.
  std::array<std::uint8_t, 8U> UpperHeaderBytes() const noexcept {
    return std::array<std::uint8_t, 8U>{
        static_cast<std::uint8_t>(request_id_ >> 24U),
        static_cast<std::uint8_t>((request_id_ >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((request_id_ >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(request_id_ & 0xFFU),
        protocol_version_,
        interface_version_,
        message_type_.Value(),
        return_code_.Value(),
    };
  }

  std::uint32_t DoEncodedSize() const noexcept { return kHeaderSize; }

  osp::expected<std::uint32_t, Error> DoEncodeTo(
      osp::span<std::uint8_t> out) const noexcept {
    ByteWriter writer(out);
    const bool ok = writer.WriteU32Be(message_id_.Value()) && writer.WriteU32Be(length_) &&
                    writer.WriteU32Be(request_id_) && writer.WriteU8(protocol_version_) &&
                    writer.WriteU8(interface_version_) && writer.WriteU8(message_type_.Value()) &&
                    writer.WriteU8(return_code_.Value());
    if (!ok) {
      return osp::expected<std::uint32_t, Error>::error(
          Error::InsufficientBuffer(kHeaderSize, static_cast<std::uint32_t>(out.size())));
    }
    return osp::expected<std::uint32_t, Error>::success(static_cast<std::uint32_t>(
        writer.Position()));
  }

 private:
  constexpr Header(MessageId message_id, std::uint32_t length, std::uint32_t request_id,
                   std::uint8_t protocol_version, std::uint8_t interface_version,
                   MessageTypeField message_type, ReturnCode return_code) noexcept
      : message_id_(message_id),
        length_(length),
        request_id_(request_id),
        protocol_version_(protocol_version),
        interface_version_(interface_version),
        message_type_(message_type),
        return_code_(return_code) {}

  MessageId message_id_{};
  std::uint32_t length_{8U};
  std::uint32_t request_id_{0U};
  std::uint8_t protocol_version_{0x01U};
  std::uint8_t interface_version_{0x01U};
  MessageTypeField message_type_{};
  ReturnCode return_code_{};
};

/// @brief Zero-copy view over exactly 16 validated header bytes.
class HeaderView final {
 public:
  constexpr HeaderView() noexcept = default;

  /**
   * @brief Parse and validate a header at the front of @p buf.
   *
   * Rejects a short buffer, a protocol version other than 0x01, an unknown
   * message type byte, and an unknown return code.
   */
  static osp::expected<ParsedHeader, Error> Parse(osp::span<const std::uint8_t> buf) noexcept;

  constexpr MessageId Id() const noexcept {
    return MessageId(detail::ReadU32Be(bytes_, 0U));
  }
  constexpr std::uint32_t Length() const noexcept { return detail::ReadU32Be(bytes_, 4U); }
  constexpr std::uint32_t RequestId() const noexcept { return detail::ReadU32Be(bytes_, 8U); }
  constexpr std::uint8_t ProtocolVersion() const noexcept { return bytes_[12U]; }
  constexpr std::uint8_t InterfaceVersion() const noexcept { return bytes_[13U]; }
  constexpr MessageTypeField Type() const noexcept {
    return MessageTypeField::FromValidatedByte(bytes_[14U]);
  }
  constexpr ReturnCode Code() const noexcept {
    return ReturnCode::FromValidatedByte(bytes_[15U]);
  }
  constexpr bool IsSd() const noexcept { return Id().IsSd(); }

  constexpr std::uint32_t PayloadSize() const noexcept {
    return (Length() > 8U) ? (Length() - 8U) : 0U;
  }

  std::array<std::uint8_t, 8U> UpperHeaderBytes() const noexcept {
    return std::array<std::uint8_t, 8U>{bytes_[8U], bytes_[9U], bytes_[10U], bytes_[11U],
                                        bytes_[12U], bytes_[13U], bytes_[14U], bytes_[15U]};
  }

  /// @brief Copy the view into an owned header.
  Header ToHeader() const noexcept {
    return Header::FromFields(Id(), Length(), RequestId(), ProtocolVersion(), InterfaceVersion(),
                              Type(), Code());
  }

 private:
  constexpr explicit HeaderView(osp::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}

  osp::span<const std::uint8_t> bytes_{};
};

/// @brief Result of parsing one header: the view plus the untouched tail.
struct ParsedHeader final {
  HeaderView view;
  osp::span<const std::uint8_t> rest;
};

inline osp::expected<ParsedHeader, Error> HeaderView::Parse(
    osp::span<const std::uint8_t> buf) noexcept {
  if (buf.size() < kHeaderSize) {
    return osp::expected<ParsedHeader, Error>::error(
        Error::Incomplete(kHeaderSize, static_cast<std::uint32_t>(buf.size())));
  }
  const std::uint8_t protocol_version = buf[12U];
  if (0x01U != protocol_version) {
    return osp::expected<ParsedHeader, Error>::error(
        Error::InvalidProtocolVersion(protocol_version));
  }
  const auto type = MessageTypeField::FromByte(buf[14U]);
  if (!type.has_value()) {
    return osp::expected<ParsedHeader, Error>::error(type.get_error());
  }
  const auto code = ReturnCode::FromByte(buf[15U]);
  if (!code.has_value()) {
    return osp::expected<ParsedHeader, Error>::error(code.get_error());
  }
  ParsedHeader parsed;
  parsed.view = HeaderView(osp::span<const std::uint8_t>(buf.data(), kHeaderSize));
  parsed.rest =
      osp::span<const std::uint8_t>(buf.data() + kHeaderSize, buf.size() - kHeaderSize);
  return osp::expected<ParsedHeader, Error>::success(parsed);
}

}  // namespace osp::someip

#endif  // OSP_SOMEIP_HEADER_HPP_
