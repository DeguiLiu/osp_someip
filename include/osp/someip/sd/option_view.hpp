/**
 * @file option_view.hpp
 * @brief Zero-copy view over one length-prefixed SD option.
 *
 * Parse() validates the length field against the buffer and, for a known
 * type, the per-type length and transport-protocol byte. An option of an
 * unknown type is accepted and keeps its place in the options run, so the
 * option indices of later entries stay meaningful.
 */

#ifndef OSP_SOMEIP_SD_OPTION_VIEW_HPP_
#define OSP_SOMEIP_SD_OPTION_VIEW_HPP_

#include "osp/someip/error.hpp"
#include "osp/someip/sd/options.hpp"
#include "osp/someip/sd/types.hpp"
#include "osp/someip/wire.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace osp::someip::sd {

class OptionView;
struct ParsedOption;

/// @brief Validated zero-copy view over one SD option.
class OptionView final {
 public:
  constexpr OptionView() noexcept = default;

  /// @brief Parse and validate one option at the front of @p buf.
  static osp::expected<ParsedOption, Error> Parse(
      osp::span<const std::uint8_t> buf) noexcept;

  /// @brief True when the type byte is one of the eight known option types.
  constexpr bool IsKnown() const noexcept {
    switch (RawType()) {
      case 0x01U:
      case 0x02U:
      case 0x04U:
      case 0x06U:
      case 0x14U:
      case 0x16U:
      case 0x24U:
      case 0x26U:
        return true;
      default:
        return false;
    }
  }

  /// @brief Option type; precondition: IsKnown().
  constexpr OptionType Type() const noexcept { return static_cast<OptionType>(RawType()); }
  constexpr std::uint8_t RawType() const noexcept { return bytes_[2U]; }
  constexpr bool Discardable() const noexcept { return 0U != (bytes_[3U] & 0x01U); }
  constexpr std::uint16_t LengthField() const noexcept { return detail::ReadU16Be(bytes_, 0U); }
  constexpr std::uint32_t WireSize() const noexcept {
    return static_cast<std::uint32_t>(LengthField()) + kLengthDelta;
  }

  /// @brief Configuration string bytes (Configuration options).
  constexpr osp::span<const std::uint8_t> Configuration() const noexcept {
    const std::uint32_t string_len =
        (LengthField() > 0U) ? static_cast<std::uint32_t>(LengthField() - 1U) : 0U;
    return osp::span<const std::uint8_t>(bytes_.data() + kPayloadOffset, string_len);
  }

  constexpr std::uint16_t Priority() const noexcept {
    return detail::ReadU16Be(bytes_, kPayloadOffset);
  }
  constexpr std::uint16_t Weight() const noexcept {
    return detail::ReadU16Be(bytes_, kPayloadOffset + 2U);
  }

  std::array<std::uint8_t, 4U> AddressV4() const noexcept {
    return std::array<std::uint8_t, 4U>{bytes_[kPayloadOffset], bytes_[kPayloadOffset + 1U],
                                        bytes_[kPayloadOffset + 2U], bytes_[kPayloadOffset + 3U]};
  }

  std::array<std::uint8_t, 16U> AddressV6() const noexcept {
    std::array<std::uint8_t, 16U> address{};
    for (std::size_t i = 0U; i < address.size(); ++i) {
      address[i] = bytes_[kPayloadOffset + i];
    }
    return address;
  }

  constexpr TransportProtocol Protocol() const noexcept {
    return static_cast<TransportProtocol>(bytes_[ProtocolOffset()]);
  }

  constexpr std::uint16_t Port() const noexcept {
    return detail::ReadU16Be(bytes_, ProtocolOffset() + 1U);
  }

  /// @brief Rebuild the owned option; fails only for an unknown type.
  osp::expected<Options, Error> ToOptions() const noexcept {
    if (!IsKnown()) {
      return osp::expected<Options, Error>::error(Error::InvalidOptionType(RawType()));
    }
    switch (Type()) {
      case OptionType::kConfiguration:
        return osp::expected<Options, Error>::success(Options::Configuration(Configuration()));
      case OptionType::kLoadBalancing:
        return osp::expected<Options, Error>::success(Options::LoadBalancing(Priority(), Weight()));
      case OptionType::kIpV4Endpoint:
        return osp::expected<Options, Error>::success(
            Options::IpV4Endpoint(AddressV4(), Protocol(), Port()));
      case OptionType::kIpV6Endpoint:
        return osp::expected<Options, Error>::success(
            Options::IpV6Endpoint(AddressV6(), Protocol(), Port()));
      case OptionType::kIpV4Multicast:
        return osp::expected<Options, Error>::success(
            Options::IpV4Multicast(AddressV4(), Protocol(), Port()));
      case OptionType::kIpV6Multicast:
        return osp::expected<Options, Error>::success(
            Options::IpV6Multicast(AddressV6(), Protocol(), Port()));
      case OptionType::kIpV4Sd:
        return osp::expected<Options, Error>::success(
            Options::IpV4Sd(AddressV4(), Protocol(), Port()));
      case OptionType::kIpV6Sd:
      default:
        return osp::expected<Options, Error>::success(
            Options::IpV6Sd(AddressV6(), Protocol(), Port()));
    }
  }

 private:
  static constexpr std::uint32_t kHeaderBytes = 4U;
  /// A length field of N covers N bytes after the 2-byte length field and
  /// the type byte, so the wire size is N + 3.
  static constexpr std::uint32_t kLengthDelta = 3U;
  static constexpr std::size_t kPayloadOffset = 4U;
  static constexpr std::size_t kIpV4ProtocolOffset = 9U;
  static constexpr std::size_t kIpV6ProtocolOffset = 21U;

  constexpr std::size_t ProtocolOffset() const noexcept {
    switch (RawType()) {
      case 0x06U:
      case 0x16U:
      case 0x26U:
        return kIpV6ProtocolOffset;
      default:
        return kIpV4ProtocolOffset;
    }
  }

  static constexpr Error Validate(std::uint8_t type, std::uint16_t length,
                                  osp::span<const std::uint8_t> buf) noexcept {
    Error err = Error::None();
    switch (type) {
      case 0x04U:
      case 0x14U:
      case 0x24U:
        err = (9U != length) ? Error::InvalidOptionLength(9U, length)
                             : ValidateProtocol(buf, kIpV4ProtocolOffset);
        break;
      case 0x06U:
      case 0x16U:
      case 0x26U:
        err = (21U != length) ? Error::InvalidOptionLength(21U, length)
                              : ValidateProtocol(buf, kIpV6ProtocolOffset);
        break;
      case 0x02U:
        err = (5U != length) ? Error::InvalidOptionLength(5U, length) : Error::None();
        break;
      case 0x01U:
        err = ((length > 0U) && ((length - 1U) > kMaxConfigurationStringLength))
                  ? Error::ConfigurationStringTooLong(length - 1U)
                  : Error::None();
        break;
      default:
        err = Error::None();
        break;
    }
    return err;
  }

  static constexpr Error ValidateProtocol(osp::span<const std::uint8_t> buf,
                                          std::size_t offset) noexcept {
    Error err = Error::IncorrectOptionsSize(static_cast<std::uint32_t>(offset + 1U),
                                            static_cast<std::uint32_t>(buf.size()));
    if (buf.size() > offset) {
      const std::uint8_t protocol = buf[offset];
      err = (0x11U == protocol || 0x06U == protocol)
                ? Error::None()
                : Error::InvalidOptionProtocol(protocol);
    }
    return err;
  }

  constexpr explicit OptionView(osp::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}

  osp::span<const std::uint8_t> bytes_{};
};

/// @brief Parsed option plus the bytes that follow it.
struct ParsedOption final {
  OptionView view;
  osp::span<const std::uint8_t> rest;
};

inline osp::expected<ParsedOption, Error> OptionView::Parse(
    osp::span<const std::uint8_t> buf) noexcept {
  if (buf.size() < kHeaderBytes) {
    return osp::expected<ParsedOption, Error>::error(
        Error::Incomplete(kHeaderBytes, static_cast<std::uint32_t>(buf.size())));
  }
  const std::uint16_t length = detail::ReadU16Be(buf, 0U);
  const std::uint32_t wire_size = static_cast<std::uint32_t>(length) + kLengthDelta;
  if (buf.size() < wire_size) {
    return osp::expected<ParsedOption, Error>::error(
        Error::IncorrectOptionsSize(wire_size, static_cast<std::uint32_t>(buf.size())));
  }
  const Error err = Validate(buf[2U], length, buf);
  if (ErrorCode::kNone != err.code) {
    return osp::expected<ParsedOption, Error>::error(err);
  }
  ParsedOption parsed;
  parsed.view = OptionView(osp::span<const std::uint8_t>(buf.data(), wire_size));
  parsed.rest =
      osp::span<const std::uint8_t>(buf.data() + wire_size, buf.size() - wire_size);
  return osp::expected<ParsedOption, Error>::success(parsed);
}

/// @brief Walks a pre-validated options section.
class OptionWalker final {
 public:
  constexpr OptionWalker() noexcept = default;
  explicit OptionWalker(osp::span<const std::uint8_t> options) noexcept : rest_(options) {}

  /// @brief Parse the next option; false once the section is exhausted.
  bool Next(OptionView& out) noexcept {
    if (rest_.empty()) {
      return false;
    }
    auto parsed = OptionView::Parse(rest_);
    if (!parsed.has_value()) {
      rest_ = osp::span<const std::uint8_t>();
      return false;
    }
    out = parsed.value().view;
    rest_ = parsed.value().rest;
    return true;
  }

  osp::span<const std::uint8_t> Remaining() const noexcept { return rest_; }

 private:
  osp::span<const std::uint8_t> rest_{};
};

}  // namespace osp::someip::sd

#endif  // OSP_SOMEIP_SD_OPTION_VIEW_HPP_
