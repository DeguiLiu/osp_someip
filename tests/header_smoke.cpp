/**
 * @file header_smoke.cpp
 * @brief Compile-only translation unit: the umbrella header plus a server
 *        instantiation must build under the library's interface flags.
 */

#include "osp/someip/server.hpp"
#include "osp/someip/someip.hpp"

#include <cstdint>

namespace {

struct Sink final {
  void operator()(osp::span<const std::uint8_t> /*datagram*/,
                  const osp::someip::Ipv4Endpoint& /*to*/) const noexcept {}
};

}  // namespace

void OspSomeipHeaderSmoke() {
  Sink sink;
  osp::someip::Server<Sink, 2U, 2U> server(sink);
  const osp::someip::ServiceInstance service =
      osp::someip::ServiceInstance::New(0x1234U, 0x0001U, 1U);
  (void)server.Offer(service, {{127U, 0U, 0U, 1U}, 30509U}, 3U, 0U);

  std::uint8_t buffer[64] = {};
  const auto built = osp::someip::BuildFindServiceDatagram(
      osp::make_span(buffer), 0x1234U, osp::someip::sd::SdSessionCounter::New().Next());
  (void)built;
}
