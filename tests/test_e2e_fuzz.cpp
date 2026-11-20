/**
 * @file test_e2e_fuzz.cpp
 * @brief Deterministic random-input sweep over the E2E profiles.
 *
 * Any buffer may be rejected, but no profile may ever report success with a
 * payload that does not fit inside the input, and every buffer produced by
 * Protect() must pass a fresh Check() with the same payload. Run under
 * ASan/UBSan for full value.
 */

#include "osp/someip/e2e_profile1.hpp"
#include "osp/someip/e2e_profile2.hpp"
#include "osp/someip/e2e_profile4.hpp"
#include "osp/someip/e2e_profile5.hpp"
#include "osp/someip/e2e_profile7.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::array<std::uint8_t, 8U> kUpperHeader{0U, 1U, 0U, 1U, 1U, 1U, 0U, 0U};

std::uint32_t NextRandom(std::uint32_t& state) noexcept {
  state = (state * 1664525U) + 1013904223U;
  return state >> 16U;
}

template <typename Profile>
void Sweep(Profile& sender, Profile& receiver, std::uint32_t header_size) {
  std::uint32_t state = 0xE2E00001U;
  std::array<std::uint8_t, 96U> buffer{};
  std::array<std::uint8_t, 96U> input{};

  for (std::uint32_t round = 0U; round < 2000U; ++round) {
    const std::uint32_t length = NextRandom(state) % (buffer.size() + 1U);
    for (std::uint32_t index = 0U; index < length; ++index) {
      input[index] = static_cast<std::uint8_t>(NextRandom(state));
    }
    const auto check = receiver.Check(
        osp::someip::MessageId(0x12340001U), kUpperHeader,
        osp::span<const std::uint8_t>(input.data(), length));
    if (check.ok) {
      REQUIRE(header_size <= length);
      REQUIRE(check.payload.size() == length - header_size);
    }

    // Protect on a copy and require a fresh receiver to accept it.
    const std::uint32_t payload_size = NextRandom(state) % (buffer.size() - header_size + 1U);
    std::memcpy(buffer.data(), input.data(), payload_size);
    const auto protected_body = sender.Protect(
        osp::someip::MessageId(0x12340001U), kUpperHeader,
        osp::span<std::uint8_t>(buffer.data(), buffer.size()), payload_size);
    REQUIRE(protected_body.size() == payload_size + header_size);
    Profile fresh;
    const auto accepted = fresh.Check(osp::someip::MessageId(0x12340001U), kUpperHeader,
                                      protected_body);
    REQUIRE(accepted.ok);
    REQUIRE(accepted.payload.size() == payload_size);
    REQUIRE(0 == std::memcmp(accepted.payload.data(), input.data(), payload_size));
  }
}

}  // namespace

TEST_CASE("e2e fuzz - profile 1 never escapes its buffer", "[fuzz][e2e]") {
  osp::someip::E2EProfile1<0x1234U> sender;
  osp::someip::E2EProfile1<0x1234U> receiver;
  Sweep(sender, receiver, 2U);
}

TEST_CASE("e2e fuzz - profile 4 never escapes its buffer", "[fuzz][e2e]") {
  osp::someip::E2EProfile4<0x1234U, 1U, 15U> sender;
  osp::someip::E2EProfile4<0x1234U, 1U, 15U> receiver;
  Sweep(sender, receiver, 12U);
}

TEST_CASE("e2e fuzz - profile 7 never escapes its buffer", "[fuzz][e2e]") {
  osp::someip::E2EProfile7<0x1234U, 15U> sender;
  osp::someip::E2EProfile7<0x1234U, 15U> receiver;
  Sweep(sender, receiver, 20U);
}

TEST_CASE("e2e fuzz - profile 5 never escapes its buffer", "[fuzz][e2e]") {
  osp::someip::E2EProfile5<0x1234U, 15U> sender;
  osp::someip::E2EProfile5<0x1234U, 15U> receiver;
  Sweep(sender, receiver, 3U);
}

TEST_CASE("e2e fuzz - profile 2 never escapes its buffer", "[fuzz][e2e]") {
  using Profile = osp::someip::E2EProfile2<osp::someip::SequentialP2Ids<0x10U>, 15U, 15U, 1U>;
  Profile sender;
  Profile receiver;
  Sweep(sender, receiver, 2U);
}
