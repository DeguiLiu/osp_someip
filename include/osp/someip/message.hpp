/**
 * @file message.hpp
 * @brief Complete SOME/IP message: owned encoder, zero-copy view, and the
 *        datagram walker that skips SOME/IP-TP segments.
 */

#ifndef OSP_SOMEIP_MESSAGE_HPP_
#define OSP_SOMEIP_MESSAGE_HPP_

#include "osp/someip/error.hpp"
#include "osp/someip/header.hpp"
#include "osp/someip/payload.hpp"
#include "osp/someip/wire.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <cstdint>

namespace osp::someip {

class MessageView;
struct DecodedMessage;

/// @brief Reason codes carried by ErrorCode::kInvalidMessage.
enum class SdRejectReason : std::uint16_t {
  kPayloadTooShort = 1U,
  kInterfaceVersion = 2U,
  kMessageType = 3U,
  kReturnCode = 4U,
};

/// @brief Header + payload value; payload type comes from the schema.
template <typename Payload>
class Message final : public WireEncoder<Message<Payload>> {
 public:
  constexpr Message(const Header& header, Payload payload) noexcept
      : header_(header), payload_(payload) {}

  constexpr const Header& Head() const noexcept { return header_; }
  constexpr const Payload& Body() const noexcept { return payload_; }
  constexpr bool IsSd() const noexcept { return header_.IsSd(); }

  std::uint32_t DoEncodedSize() const noexcept {
    return kHeaderSize + payload_.EncodedSize();
  }

  osp::expected<std::uint32_t, Error> DoEncodeTo(
      osp::span<std::uint8_t> out) const noexcept {
    osp::span<std::uint8_t> header_out(out.data(), (out.size() < kHeaderSize) ? out.size()
                                                                             : kHeaderSize);
    auto header_result = header_.EncodeTo(header_out);
    if (!header_result.has_value()) {
      return osp::expected<std::uint32_t, Error>::error(header_result.get_error());
    }
    osp::span<std::uint8_t> payload_out(out.data() + kHeaderSize, out.size() - kHeaderSize);
    auto payload_result = payload_.EncodeTo(payload_out);
    if (!payload_result.has_value()) {
      return osp::expected<std::uint32_t, Error>::error(payload_result.get_error());
    }
    return osp::expected<std::uint32_t, Error>::success(kHeaderSize + payload_result.value());
  }

 private:
  Header header_;
  Payload payload_;
};

/// @brief Zero-copy view over one complete SOME/IP message.
class MessageView final {
 public:
  constexpr MessageView() noexcept = default;

  static osp::expected<MessageView, Error> Parse(
      osp::span<const std::uint8_t> buf) noexcept;

  /// @brief Decode one message from the front of @p buf.
  static osp::expected<DecodedMessage, Error> Decode(
      osp::span<const std::uint8_t> buf) noexcept;

  constexpr HeaderView Head() const noexcept { return header_; }
  constexpr osp::span<const std::uint8_t> Body() const noexcept { return payload_; }
  constexpr bool IsSd() const noexcept { return header_.IsSd(); }

 private:
  constexpr MessageView(HeaderView header, osp::span<const std::uint8_t> payload) noexcept
      : header_(header), payload_(payload) {}

  static Error Check(const HeaderView& header, osp::span<const std::uint8_t> rest) noexcept {
    Error err = Error::None();
    if (header.Length() < 8U) {
      err = Error::InvalidLength(header.Length());
    } else if (rest.size() < header.PayloadSize()) {
      err = Error::Incomplete(header.PayloadSize(), static_cast<std::uint32_t>(rest.size()));
    } else if (header.IsSd()) {
      err = CheckSd(header);
    }
    return err;
  }

  static Error CheckSd(const HeaderView& header) noexcept {
    Error err = Error::None();
    if (header.PayloadSize() < 12U) {
      err = Error::InvalidMessage(static_cast<std::uint16_t>(SdRejectReason::kPayloadTooShort));
    } else if (0x01U != header.InterfaceVersion()) {
      err = Error::InvalidMessage(static_cast<std::uint16_t>(SdRejectReason::kInterfaceVersion));
    } else if (MessageType::kNotification != header.Type().Type() || header.Type().IsTp()) {
      err = Error::InvalidMessage(static_cast<std::uint16_t>(SdRejectReason::kMessageType));
    } else if (header.Code() != ReturnCode::Ok()) {
      err = Error::InvalidMessage(static_cast<std::uint16_t>(SdRejectReason::kReturnCode));
    }
    return err;
  }

  HeaderView header_{};
  osp::span<const std::uint8_t> payload_{};
};

/// @brief Decoded view plus the bytes that follow the declared payload.
struct DecodedMessage final {
  MessageView view;
  osp::span<const std::uint8_t> rest;
};

inline osp::expected<MessageView, Error> MessageView::Parse(
    osp::span<const std::uint8_t> buf) noexcept {
  auto decoded = Decode(buf);
  if (!decoded.has_value()) {
    return osp::expected<MessageView, Error>::error(decoded.get_error());
  }
  return osp::expected<MessageView, Error>::success(decoded.value().view);
}

inline osp::expected<DecodedMessage, Error> MessageView::Decode(
    osp::span<const std::uint8_t> buf) noexcept {
  auto parsed = HeaderView::Parse(buf);
  if (!parsed.has_value()) {
    return osp::expected<DecodedMessage, Error>::error(parsed.get_error());
  }
  const ParsedHeader& fields = parsed.value();
  const Error err = Check(fields.view, fields.rest);
  if (ErrorCode::kNone != err.code) {
    return osp::expected<DecodedMessage, Error>::error(err);
  }
  const std::uint32_t payload_size = fields.view.PayloadSize();
  DecodedMessage decoded;
  decoded.view =
      MessageView(fields.view, osp::span<const std::uint8_t>(fields.rest.data(), payload_size));
  decoded.rest =
      (0U == payload_size)
          ? fields.rest
          : osp::span<const std::uint8_t>(fields.rest.data() + payload_size,
                                          fields.rest.size() - payload_size);
  return osp::expected<DecodedMessage, Error>::success(decoded);
}

/// @brief One item produced by MessageWalker.
struct WalkItem final {
  enum class Kind : std::uint8_t { kMessage = 0U, kMalformed };

  Kind kind{Kind::kMessage};
  MessageView message{};
  HeaderView malformed_header{};
  Error error{};
  osp::span<const std::uint8_t> bytes{};
};

/**
 * @brief Walk every SOME/IP message in a UDP datagram.
 *
 * Each message ends where its Length field says and the next one starts right
 * after it. SOME/IP-TP segments are counted and skipped. The walk stops at
 * the first malformed message, and at bytes that hold no header (or a TP
 * segment running past the datagram), which `Unparsed()` then returns.
 */
class MessageWalker final {
 public:
  explicit MessageWalker(osp::span<const std::uint8_t> datagram) noexcept : rest_(datagram) {}

  bool Next(WalkItem& item) noexcept {
    while (!rest_.empty()) {
      auto decoded = MessageView::Decode(rest_);
      if (decoded.has_value()) {
        rest_ = decoded.value().rest;
        if (decoded.value().view.Head().Type().IsTp()) {
          ++skipped_tp_segments_;
          continue;
        }
        item = WalkItem{};
        item.kind = WalkItem::Kind::kMessage;
        item.message = decoded.value().view;
        return true;
      }
      auto header = HeaderView::Parse(rest_);
      if (!header.has_value()) {
        EndUnparsed();
        return false;
      }
      const HeaderView view = header.value().view;
      if (view.Type().IsTp() && !view.IsSd()) {
        ++skipped_tp_segments_;
        EndUnparsed();
        return false;
      }
      item = WalkItem{};
      item.kind = WalkItem::Kind::kMalformed;
      item.malformed_header = view;
      item.error = decoded.get_error();
      item.bytes = rest_;
      rest_ = osp::span<const std::uint8_t>();
      return true;
    }
    return false;
  }

  osp::span<const std::uint8_t> Remaining() const noexcept { return rest_; }
  osp::span<const std::uint8_t> Unparsed() const noexcept { return unparsed_; }
  std::uint32_t SkippedTpSegments() const noexcept { return skipped_tp_segments_; }

 private:
  void EndUnparsed() noexcept {
    unparsed_ = rest_;
    rest_ = osp::span<const std::uint8_t>();
  }

  osp::span<const std::uint8_t> rest_;
  osp::span<const std::uint8_t> unparsed_{};
  std::uint32_t skipped_tp_segments_{0U};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_MESSAGE_HPP_
