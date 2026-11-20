/**
 * @file options.hpp
 * @brief SOME/IP-SD option values and their wire encoding.
 *
 * Every option is laid out as `length(2) | type(1) | discard(1) | payload`.
 * The length field counts the discard byte and the payload, but neither the
 * length field itself nor the type byte; that is the value the reference
 * implementation and vsomeip put on the wire.
 *
 * Configuration strings are copied into the value, so an option can outlive
 * the buffer it was built from. Nothing here allocates, throws, or dispatches
 * virtually.
 */

#ifndef OSP_SOMEIP_SD_OPTIONS_HPP_
#define OSP_SOMEIP_SD_OPTIONS_HPP_

#include "osp/platform.hpp"
#include "osp/someip/error.hpp"
#include "osp/someip/sd/types.hpp"
#include "osp/someip/wire.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace osp::someip::sd {

/// @brief One SD option value; build it with the named factory functions.
class Options final : public osp::someip::WireEncoder<Options> {
 public:
  /// @brief Option variant; the numeric values are the on-wire type bytes.
  enum class Kind : std::uint8_t {
    kConfiguration = 0x01U,
    kLoadBalancing = 0x02U,
    kIpV4Endpoint = 0x04U,
    kIpV6Endpoint = 0x06U,
    kIpV4Multicast = 0x14U,
    kIpV6Multicast = 0x16U,
    kIpV4Sd = 0x24U,
    kIpV6Sd = 0x26U,
  };

  /// @brief Empty configuration option; valid wire value, used for storage.
  Options() noexcept = default;

  /// @brief Configuration option holding an arbitrary byte string.
  static Options Configuration(osp::span<const std::uint8_t> text) noexcept {
    OSP_ASSERT(text.size() <= static_cast<std::size_t>(kMaxConfigurationStringLength));
    Options option(Kind::kConfiguration);
    option.text_length_ = static_cast<std::uint16_t>(text.size());
    for (std::size_t i = 0U; i < text.size(); ++i) {
      option.text_[i] = text[i];
    }
    return option;
  }

  /// @brief Load-balancing option.
  static Options LoadBalancing(std::uint16_t priority, std::uint16_t weight) noexcept {
    Options option(Kind::kLoadBalancing);
    option.priority_ = priority;
    option.weight_ = weight;
    return option;
  }

  /// @brief IPv4 endpoint option.
  static Options IpV4Endpoint(const std::array<std::uint8_t, 4>& address,
                              TransportProtocol protocol, std::uint16_t port) noexcept {
    return MakeIpV4Option(Kind::kIpV4Endpoint, address, protocol, port);
  }

  /// @brief IPv6 endpoint option.
  static Options IpV6Endpoint(const std::array<std::uint8_t, 16>& address,
                              TransportProtocol protocol, std::uint16_t port) noexcept {
    return MakeIpV6Option(Kind::kIpV6Endpoint, address, protocol, port);
  }

  /// @brief IPv4 multicast option.
  static Options IpV4Multicast(const std::array<std::uint8_t, 4>& address,
                               TransportProtocol protocol, std::uint16_t port) noexcept {
    return MakeIpV4Option(Kind::kIpV4Multicast, address, protocol, port);
  }

  /// @brief IPv6 multicast option.
  static Options IpV6Multicast(const std::array<std::uint8_t, 16>& address,
                               TransportProtocol protocol, std::uint16_t port) noexcept {
    return MakeIpV6Option(Kind::kIpV6Multicast, address, protocol, port);
  }

  /// @brief IPv4 SD endpoint option.
  static Options IpV4Sd(const std::array<std::uint8_t, 4>& address,
                        TransportProtocol protocol, std::uint16_t port) noexcept {
    return MakeIpV4Option(Kind::kIpV4Sd, address, protocol, port);
  }

  /// @brief IPv6 SD endpoint option.
  static Options IpV6Sd(const std::array<std::uint8_t, 16>& address,
                        TransportProtocol protocol, std::uint16_t port) noexcept {
    return MakeIpV6Option(Kind::kIpV6Sd, address, protocol, port);
  }

  /// @brief Which option variant this value holds.
  Kind Type() const noexcept { return kind_; }

  /// @brief Total wire size including the 4-byte option header.
  std::uint32_t DoEncodedSize() const noexcept {
    switch (kind_) {
      case Kind::kConfiguration:
        return kOptionHeaderSize + text_length_;
      case Kind::kLoadBalancing:
        return kLoadBalancingOptionSize;
      case Kind::kIpV4Endpoint:
      case Kind::kIpV4Multicast:
      case Kind::kIpV4Sd:
        return kIpV4OptionSize;
      case Kind::kIpV6Endpoint:
      case Kind::kIpV6Multicast:
      case Kind::kIpV6Sd:
        return kIpV6OptionSize;
    }
    return kOptionHeaderSize;
  }

  /// @brief Encode into @p out; a short slice yields kInsufficientBuffer.
  osp::expected<std::uint32_t, osp::someip::Error> DoEncodeTo(
      osp::span<std::uint8_t> out) const noexcept {
    const std::uint32_t size = DoEncodedSize();
    if (static_cast<std::size_t>(size) > out.size()) {
      return osp::expected<std::uint32_t, osp::someip::Error>::error(
          osp::someip::Error::InsufficientBuffer(size,
                                                 static_cast<std::uint32_t>(out.size())));
    }
    osp::someip::ByteWriter writer(out);
    const std::uint16_t length = static_cast<std::uint16_t>(size - kLengthSizeDelta);
    const bool written = WriteHeader(writer, length) && WritePayload(writer);
    OSP_ASSERT(written);
    (void)written;
    return osp::expected<std::uint32_t, osp::someip::Error>::success(size);
  }

  /// @brief Configuration string; empty for every other variant.
  osp::span<const std::uint8_t> Text() const noexcept {
    return osp::span<const std::uint8_t>(text_.data(), text_length_);
  }

  /// @brief Load-balancing priority; zero for other variants.
  std::uint16_t Priority() const noexcept { return priority_; }

  /// @brief Load-balancing weight; zero for other variants.
  std::uint16_t Weight() const noexcept { return weight_; }

  /// @brief Endpoint port; zero for configuration and load balancing.
  std::uint16_t Port() const noexcept { return port_; }

  /// @brief Endpoint transport protocol; meaningless for non-endpoint variants.
  TransportProtocol Protocol() const noexcept { return protocol_; }

  /// @brief IPv6 address; an IPv4 variant keeps its 4 octets in the first slots.
  const std::array<std::uint8_t, 16>& AddressV6() const noexcept { return address_; }

 private:
  static constexpr std::uint32_t kOptionHeaderSize = 4U;
  static constexpr std::uint32_t kLengthSizeDelta = 3U;
  static constexpr std::uint32_t kLoadBalancingOptionSize = 8U;
  static constexpr std::uint32_t kIpV4OptionSize = 12U;
  static constexpr std::uint32_t kIpV6OptionSize = 24U;

  explicit Options(Kind kind) noexcept : kind_(kind) {}

  static Options MakeIpV4Option(Kind kind, const std::array<std::uint8_t, 4>& address,
                                TransportProtocol protocol, std::uint16_t port) noexcept {
    Options option(kind);
    option.address_[0] = address[0];
    option.address_[1] = address[1];
    option.address_[2] = address[2];
    option.address_[3] = address[3];
    option.protocol_ = protocol;
    option.port_ = port;
    return option;
  }

  static Options MakeIpV6Option(Kind kind, const std::array<std::uint8_t, 16>& address,
                                TransportProtocol protocol, std::uint16_t port) noexcept {
    Options option(kind);
    option.address_ = address;
    option.protocol_ = protocol;
    option.port_ = port;
    return option;
  }

  bool WriteHeader(osp::someip::ByteWriter& writer, std::uint16_t length) const noexcept {
    return writer.WriteU16Be(length) && writer.WriteU8(static_cast<std::uint8_t>(kind_)) &&
           writer.WriteU8(0U);
  }

  bool WritePayload(osp::someip::ByteWriter& writer) const noexcept {
    bool written = false;
    switch (kind_) {
      case Kind::kConfiguration:
        written = writer.WriteBytes(Text());
        break;
      case Kind::kLoadBalancing:
        written = writer.WriteU16Be(priority_) && writer.WriteU16Be(weight_);
        break;
      case Kind::kIpV4Endpoint:
      case Kind::kIpV4Multicast:
      case Kind::kIpV4Sd:
        written = WriteAddressTail(writer, 4U);
        break;
      case Kind::kIpV6Endpoint:
      case Kind::kIpV6Multicast:
      case Kind::kIpV6Sd:
        written = WriteAddressTail(writer, 16U);
        break;
    }
    return written;
  }

  bool WriteAddressTail(osp::someip::ByteWriter& writer, std::size_t address_size) const noexcept {
    const osp::span<const std::uint8_t> raw(address_.data(), address_size);
    return writer.WriteBytes(raw) && writer.WriteU8(0U) &&
           writer.WriteU8(static_cast<std::uint8_t>(protocol_)) && writer.WriteU16Be(port_);
  }

  Kind kind_{Kind::kConfiguration};
  std::array<std::uint8_t, 16> address_{};
  std::uint16_t priority_{0U};
  std::uint16_t weight_{0U};
  std::uint16_t port_{0U};
  TransportProtocol protocol_{TransportProtocol::kUdp};
  std::uint16_t text_length_{0U};
  std::array<std::uint8_t, static_cast<std::size_t>(kMaxConfigurationStringLength)> text_{};
};

// Kind doubles as the on-wire type byte; keep the two tables in lockstep.
static_assert(static_cast<std::uint8_t>(Options::Kind::kConfiguration) ==
                      static_cast<std::uint8_t>(OptionType::kConfiguration) &&
                  static_cast<std::uint8_t>(Options::Kind::kLoadBalancing) ==
                      static_cast<std::uint8_t>(OptionType::kLoadBalancing) &&
                  static_cast<std::uint8_t>(Options::Kind::kIpV4Endpoint) ==
                      static_cast<std::uint8_t>(OptionType::kIpV4Endpoint) &&
                  static_cast<std::uint8_t>(Options::Kind::kIpV6Endpoint) ==
                      static_cast<std::uint8_t>(OptionType::kIpV6Endpoint) &&
                  static_cast<std::uint8_t>(Options::Kind::kIpV4Multicast) ==
                      static_cast<std::uint8_t>(OptionType::kIpV4Multicast) &&
                  static_cast<std::uint8_t>(Options::Kind::kIpV6Multicast) ==
                      static_cast<std::uint8_t>(OptionType::kIpV6Multicast) &&
                  static_cast<std::uint8_t>(Options::Kind::kIpV4Sd) ==
                      static_cast<std::uint8_t>(OptionType::kIpV4Sd) &&
                  static_cast<std::uint8_t>(Options::Kind::kIpV6Sd) ==
                      static_cast<std::uint8_t>(OptionType::kIpV6Sd),
              "Options::Kind values must equal the OptionType wire bytes");

}  // namespace osp::someip::sd

#endif  // OSP_SOMEIP_SD_OPTIONS_HPP_
