/**
 * @file endpoint.hpp
 * @brief IPv4 endpoint value shared by the SD builders and the server.
 */

#ifndef OSP_SOMEIP_ENDPOINT_HPP_
#define OSP_SOMEIP_ENDPOINT_HPP_

#include <array>
#include <cstdint>

namespace osp::someip {

/// @brief IPv4 address plus UDP/TCP port.
struct Ipv4Endpoint final {
  std::array<std::uint8_t, 4U> address{};
  std::uint16_t port{0U};

  friend constexpr bool operator==(const Ipv4Endpoint& lhs, const Ipv4Endpoint& rhs) noexcept {
    return lhs.port == rhs.port && lhs.address == rhs.address;
  }
  friend constexpr bool operator!=(const Ipv4Endpoint& lhs, const Ipv4Endpoint& rhs) noexcept {
    return !(lhs == rhs);
  }
};

/// @brief Standard SOME/IP-SD multicast group and port.
inline constexpr std::array<std::uint8_t, 4U> kSdMulticastAddress{239U, 255U, 0U, 255U};
inline constexpr std::uint16_t kSdPort = 30490U;

/// @brief The default SD destination for offers and find responses.
constexpr Ipv4Endpoint SdMulticastEndpoint() noexcept {
  return Ipv4Endpoint{kSdMulticastAddress, kSdPort};
}

}  // namespace osp::someip

#endif  // OSP_SOMEIP_ENDPOINT_HPP_
