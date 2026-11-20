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
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace osp::someip {

namespace detail {

inline void FormatIpv4(const std::array<std::uint8_t, 4U>& ip,
                       char (&text)[16U]) noexcept {
  (void)std::snprintf(text, sizeof(text), "%u.%u.%u.%u", static_cast<unsigned>(ip[0]),
                      static_cast<unsigned>(ip[1]), static_cast<unsigned>(ip[2]),
                      static_cast<unsigned>(ip[3]));
}

}  // namespace detail

/// @brief Server send sink over a bound UDP socket; best effort, never blocks.
class UdpSender final {
 public:
  explicit UdpSender(osp::UdpSocket& socket) noexcept : socket_(socket) {}

  void operator()(osp::span<const std::uint8_t> datagram,
                  const Ipv4Endpoint& to) const noexcept {
    char text[16] = {};
    detail::FormatIpv4(to.address, text);
    const auto address = osp::SocketAddress::FromIpv4(text, to.port);
    if (address.has_value()) {
      (void)socket_.SendTo(datagram.data(), datagram.size(), address.value());
    }
  }

 private:
  osp::UdpSocket& socket_;
};

/// @brief Endpoint of a datagram source, for Server::HandleDatagram().
inline Ipv4Endpoint EndpointOf(const osp::SocketAddress& address) noexcept {
  std::array<std::uint8_t, 4U> octets{};
  // SocketAddress wraps the OS sockaddr; every backend used here (POSIX,
  // lwIP, Winsock) stores an IPv4 address as sockaddr_in.
  const auto* raw = reinterpret_cast<const sockaddr_in*>(address.Raw());
  std::memcpy(octets.data(), &raw->sin_addr, octets.size());
  return Ipv4Endpoint{octets, address.Port()};
}

/// @brief Join 239.255.0.255:30490 on @p interface_address (all-zero = any).
inline bool JoinSdMulticast(osp::UdpSocket& socket,
                            const std::array<std::uint8_t, 4>& interface_address) noexcept {
#if defined(IP_ADD_MEMBERSHIP)
  ip_mreq request{};
  std::memcpy(&request.imr_multiaddr, kSdMulticastAddress.data(),
              kSdMulticastAddress.size());
  std::memcpy(&request.imr_interface, interface_address.data(), interface_address.size());
  return 0 == osp::socket_api::SetSockOpt(socket.Fd(), IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                          &request, static_cast<socklen_t>(sizeof(request)));
#else
  (void)socket;
  (void)interface_address;
  return false;
#endif
}

/// @brief Select the outgoing interface for SD multicast sends.
inline bool SetSdMulticastInterface(
    osp::UdpSocket& socket,
    const std::array<std::uint8_t, 4>& interface_address) noexcept {
#if defined(IP_MULTICAST_IF)
  in_addr request{};
  std::memcpy(&request, interface_address.data(), interface_address.size());
  return 0 == osp::socket_api::SetSockOpt(socket.Fd(), IPPROTO_IP, IP_MULTICAST_IF,
                                          &request, static_cast<socklen_t>(sizeof(request)));
#else
  (void)socket;
  (void)interface_address;
  return false;
#endif
}

/// @brief Enable or disable loopback of this socket's own multicast datagrams.
inline bool SetSdMulticastLoop(osp::UdpSocket& socket, bool enable) noexcept {
#if defined(IP_MULTICAST_LOOP)
  const std::uint8_t request = enable ? 1U : 0U;
  return 0 == osp::socket_api::SetSockOpt(socket.Fd(), IPPROTO_IP, IP_MULTICAST_LOOP,
                                          &request, static_cast<socklen_t>(sizeof(request)));
#else
  (void)socket;
  (void)enable;
  return false;
#endif
}

}  // namespace osp::someip

#endif  // OSP_SOMEIP_UDP_LINK_HPP_
