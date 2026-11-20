/**
 * @file test_sd_fuzz.cpp
 * @brief Deterministic random-input sweep over every parser entry point.
 *
 * The point is memory safety and invariant preservation, not protocol
 * correctness: any input may be rejected, but accepted entries and options
 * must round-trip through their owned value types without escaping the
 * buffer. Run under ASan/UBSan for full value.
 */

#include "osp/someip/message.hpp"
#include "osp/someip/sd/entry_view.hpp"
#include "osp/someip/sd/option_view.hpp"
#include "osp/someip/sd_codec.hpp"

#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

namespace {

std::uint32_t NextRandom(std::uint32_t& state) noexcept {
  state = (state * 1664525U) + 1013904223U;
  return state >> 16U;
}

}  // namespace

TEST_CASE("sd fuzz - random datagrams never escape the parsers", "[fuzz][sd]") {
  std::uint32_t state = 0xC0FFEE01U;
  std::uint8_t buffer[192] = {};
  std::uint8_t seed[128] = {};

  osp::someip::OfferServiceRequest request;
  request.service_id = 0x1234U;
  request.instance_id = 0x0001U;
  request.major_version = 1U;
  request.ttl = 3U;
  request.local_ip = {192U, 0U, 2U, 1U};
  request.unicast_port = 30501U;
  const auto seed_size = osp::someip::BuildOfferServiceDatagram(
      osp::make_span(seed), request, osp::someip::sd::SdSessionCounter::New().Next());
  REQUIRE(seed_size.has_value());

  for (std::uint32_t iteration = 0U; iteration < 3000U; ++iteration) {
    std::size_t length = 0U;
    if (0U == (iteration % 2U)) {
      length = seed_size.value();
      std::memcpy(buffer, seed, length);
      const std::uint32_t flips = 1U + (NextRandom(state) % 4U);
      for (std::uint32_t flip = 0U; flip < flips; ++flip) {
        const std::size_t at = NextRandom(state) % length;
        buffer[at] = static_cast<std::uint8_t>(
            buffer[at] ^ (1U << (NextRandom(state) % 8U)));
      }
    } else {
      length = NextRandom(state) % sizeof(buffer);
      for (std::size_t index = 0U; index < length; ++index) {
        buffer[index] = static_cast<std::uint8_t>(NextRandom(state) & 0xFFU);
      }
    }
    const auto view = osp::make_span(buffer, length);

    const auto header = osp::someip::HeaderView::Parse(view);
    if (header.has_value()) {
      (void)header.value().view.ToHeader();
    }

    const auto message = osp::someip::MessageView::Parse(view);
    if (message.has_value() && message.value().IsSd()) {
      auto sd = message.value().SdHeader();
      if (sd.has_value()) {
        osp::someip::sd::EntryView entry;
        while (sd.value().NextEntry(entry)) {
          const auto owned = entry.ToEntry();
          std::uint8_t entry_bytes[16] = {};
          const auto written = owned.EncodeTo(osp::make_span(entry_bytes));
          REQUIRE(written.has_value());
          REQUIRE(16U == written.value());
        }
        osp::someip::sd::OptionView option;
        while (sd.value().NextOption(option)) {
          if (option.IsKnown()) {
            const auto owned = option.ToOptions();
            REQUIRE(owned.has_value());
            std::uint8_t option_bytes[300] = {};
            const auto written = owned.value().EncodeTo(osp::make_span(option_bytes));
            REQUIRE(written.has_value());
            REQUIRE(owned.value().EncodedSize() == written.value());
          }
        }
      }
    }

    osp::someip::MessageWalker walker(view);
    osp::someip::WalkItem item;
    while (walker.Next(item)) {
    }
  }
}
