/**
 * @file no_alloc_witness.cpp
 * @brief Runtime witness that the offer/subscribe/publish path allocates nothing.
 *
 * Global operator new/delete are replaced with counting versions. The binary
 * drives one full scenario and returns non-zero if a single heap allocation
 * happened, so any future `std::vector`, `std::string` or owning wrapper in
 * the hot path fails this test instead of silently shipping.
 */

#include "osp/someip/sd_codec.hpp"
#include "osp/someip/server.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace {

std::size_t g_allocations = 0U;

struct Sink final {
  std::uint32_t datagrams{0U};
  osp::someip::Ipv4Endpoint last_to{};

  void operator()(osp::span<const std::uint8_t> /*datagram*/,
                  const osp::someip::Ipv4Endpoint& to) noexcept {
    ++datagrams;
    last_to = to;
  }
};

}  // namespace

void* operator new(std::size_t size) {
  ++g_allocations;
  void* memory = std::malloc(0U == size ? 1U : size);
  if (nullptr == memory) {
    std::abort();
  }
  return memory;
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t /*size*/) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t /*size*/) noexcept { std::free(memory); }

int main() {
  bool ok = true;
  Sink sink;
  osp::someip::Server<Sink, 3U, 3U> server(sink);
  const osp::someip::ServiceInstance service =
      osp::someip::ServiceInstance::New(0x1234U, 0x0001U, 1U);

  ok = ok && server.Offer(service, {{127U, 0U, 0U, 1U}, 30509U}, 30U, 0U);
  ok = ok && 1U == sink.datagrams;

  std::uint8_t find[64] = {};
  const auto find_size = osp::someip::BuildFindServiceDatagram(
      osp::make_span(find), 0x1234U,
      osp::someip::sd::SdSessionCounter::New().Next());
  ok = ok && find_size.has_value();
  const osp::someip::Ipv4Endpoint client{{127U, 0U, 0U, 2U}, 30490U};
  server.HandleDatagram(osp::make_span(find, find_size.value()), client, 10U);
  ok = ok && 2U == sink.datagrams;
  ok = ok && client == sink.last_to;

  osp::someip::SubscribeEventGroupRequest subscribe_request;
  subscribe_request.service_id = 0x1234U;
  subscribe_request.instance_id = 0x0001U;
  subscribe_request.major_version = 1U;
  subscribe_request.event_group_id = 0x0001U;
  subscribe_request.ttl = 5U;
  subscribe_request.local_ip = {127U, 0U, 0U, 2U};
  subscribe_request.local_rx_port = 30510U;
  std::uint8_t subscribe[128] = {};
  auto counter = osp::someip::sd::SdSessionCounter::New();
  const auto subscribe_size = osp::someip::BuildSubscribeEventGroupDatagram(
      osp::make_span(subscribe), subscribe_request, counter.Next());
  ok = ok && subscribe_size.has_value();
  server.HandleDatagram(osp::make_span(subscribe, subscribe_size.value()), client, 20U);
  ok = ok && 3U == sink.datagrams;
  ok = ok && 1U == server.SubscriberCount();

  const std::uint8_t payload[2] = {0x5AU, 0xA5U};
  server.Publish(service, 0x0001U, 0x8001U, osp::make_span(payload), 30U);
  ok = ok && 4U == sink.datagrams;
  ok = ok && (osp::someip::Ipv4Endpoint{{127U, 0U, 0U, 2U}, 30510U} == sink.last_to);

  server.Tick(1000U);
  ok = ok && 4U == sink.datagrams;

  return (ok && 0U == g_allocations) ? 0 : 1;
}
