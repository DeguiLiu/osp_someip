/**
 * @file udp_link.hpp
 * @brief Adapters between the SOME/IP endpoint type and newosp UDP sockets.
 */

#ifndef OSP_SOMEIP_UDP_LINK_HPP_
#define OSP_SOMEIP_UDP_LINK_HPP_

#include "osp/someip/endpoint.hpp"
#include "osp/socket.hpp"
#include "osp/span.hpp"

#include <array>
#include <cstdint>

namespace osp::someip {

/// @brief Server send sink over a bound UDP socket; best effort, never blocks.
class UdpSender final {
 public:
  explicit UdpSender(osp::UdpSocket& socket) noexcept : socket_(socket) {}

  void operator()(osp::span<const std::uint8_t> datagram,
                  const Ipv4Endpoint& to) const noexcept {
    const auto address = osp::SocketAddress::FromIpv4Bytes(to.address, to.port);
    if (address.has_value()) {
      (void)socket_.SendTo(datagram.data(), datagram.size(), address.value());
    }
  }

 private:
  osp::UdpSocket& socket_;
};

/// @brief Endpoint of a datagram source, for Server::HandleDatagram().
inline Ipv4Endpoint EndpointOf(const osp::SocketAddress& address) noexcept {
  return Ipv4Endpoint{address.Ipv4Address(), address.Port()};
}

/// @brief Join 239.255.0.255:30490 on @p interface_address (all-zero = any).
inline bool JoinSdMulticast(osp::UdpSocket& socket,
                            const std::array<std::uint8_t, 4>& interface_address) noexcept {
  const auto group = osp::SocketAddress::FromIpv4Bytes(kSdMulticastAddress, kSdPort);
  return group.has_value() &&
         socket.JoinMulticastGroup(group.value(), interface_address).has_value();
}

}  // namespace osp::someip

#endif  // OSP_SOMEIP_UDP_LINK_HPP_
