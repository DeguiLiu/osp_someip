/**
 * @file test_sd_options.cpp
 * @brief Golden-byte tests for the eight SD option variants.
 *
 * The expected arrays are hand-derived from the option layout and checked
 * against the literals of the Rust reference implementation. Encoder output is
 * never used as an expected value.
 */

#include "osp/someip/sd/options.hpp"

#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using osp::someip::ErrorCode;
using osp::someip::sd::Options;
using osp::someip::sd::TransportProtocol;

namespace {

template <std::size_t Size>
std::array<std::uint8_t, Size> Encode(const Options& option) {
  std::array<std::uint8_t, Size> out{};
  const auto result = option.EncodeTo(osp::span<std::uint8_t>(out.data(), out.size()));
  REQUIRE(result.has_value());
  REQUIRE(static_cast<std::uint32_t>(Size) == result.value());
  return out;
}

constexpr std::array<std::uint8_t, 4> kIpV4Address = {10U, 0U, 0U, 1U};
constexpr std::array<std::uint8_t, 16> kIpV6Address = {0x20U, 0x01U, 0x0DU, 0xB8U, 0U, 0U, 0U, 0U,
                                                       0U,     0U,     0U,     0U,     0U, 0U, 0U, 0x01U};

}  // namespace

TEST_CASE("sd options - configuration golden bytes", "[sd][options]") {
  const std::uint8_t text[] = {0x61U, 0x62U};
  const Options option = Options::Configuration(osp::span<const std::uint8_t>(text));
  const std::array<std::uint8_t, 6> expected = {0x00U, 0x03U, 0x01U, 0x00U, 0x61U, 0x62U};

  REQUIRE(Options::Kind::kConfiguration == option.Type());
  REQUIRE(6U == option.EncodedSize());
  REQUIRE(expected == Encode<6>(option));
  REQUIRE(2U == option.Text().size());
  REQUIRE(0x61U == option.Text()[0]);
  REQUIRE(0x62U == option.Text()[1]);
}

TEST_CASE("sd options - load balancing golden bytes", "[sd][options]") {
  const Options option = Options::LoadBalancing(0x1234U, 0x5678U);
  const std::array<std::uint8_t, 8> expected = {0x00U, 0x05U, 0x02U, 0x00U,
                                                0x12U, 0x34U, 0x56U, 0x78U};

  REQUIRE(Options::Kind::kLoadBalancing == option.Type());
  REQUIRE(8U == option.EncodedSize());
  REQUIRE(expected == Encode<8>(option));
  REQUIRE(0x1234U == option.Priority());
  REQUIRE(0x5678U == option.Weight());
}

TEST_CASE("sd options - IPv4 endpoint golden bytes", "[sd][options]") {
  const Options option = Options::IpV4Endpoint(kIpV4Address, TransportProtocol::kUdp, 30490U);
  const std::array<std::uint8_t, 12> expected = {0x00U, 0x09U, 0x04U, 0x00U, 10U,   0U,
                                                 0U,    1U,    0x00U, 0x11U, 0x77U, 0x1AU};

  REQUIRE(Options::Kind::kIpV4Endpoint == option.Type());
  REQUIRE(12U == option.EncodedSize());
  REQUIRE(expected == Encode<12>(option));
  REQUIRE(30490U == option.Port());
  REQUIRE(TransportProtocol::kUdp == option.Protocol());
  REQUIRE(10U == option.AddressV6()[0]);
  REQUIRE(1U == option.AddressV6()[3]);
}

TEST_CASE("sd options - IPv6 endpoint golden bytes", "[sd][options]") {
  const Options option = Options::IpV6Endpoint(kIpV6Address, TransportProtocol::kTcp, 443U);
  const std::array<std::uint8_t, 24> expected = {
      0x00U, 0x15U, 0x06U, 0x00U, 0x20U, 0x01U, 0x0DU, 0xB8U,
      0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
      0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x06U, 0x01U, 0xBBU};

  REQUIRE(Options::Kind::kIpV6Endpoint == option.Type());
  REQUIRE(24U == option.EncodedSize());
  REQUIRE(expected == Encode<24>(option));
  REQUIRE(443U == option.Port());
  REQUIRE(TransportProtocol::kTcp == option.Protocol());
}

TEST_CASE("sd options - IPv4 multicast golden bytes", "[sd][options]") {
  const std::array<std::uint8_t, 4> address = {239U, 1U, 2U, 3U};
  const Options option = Options::IpV4Multicast(address, TransportProtocol::kUdp, 30491U);
  const std::array<std::uint8_t, 12> expected = {0x00U, 0x09U, 0x14U, 0x00U, 239U,  1U,
                                                 2U,    3U,    0x00U, 0x11U, 0x77U, 0x1BU};

  REQUIRE(Options::Kind::kIpV4Multicast == option.Type());
  REQUIRE(12U == option.EncodedSize());
  REQUIRE(expected == Encode<12>(option));
}

TEST_CASE("sd options - IPv6 multicast golden bytes", "[sd][options]") {
  const std::array<std::uint8_t, 16> address = {0xFFU, 0x02U, 0x00U, 0x00U, 0x00U, 0x00U,
                                                0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                                                0x00U, 0x00U, 0xABU, 0xCDU};
  const Options option = Options::IpV6Multicast(address, TransportProtocol::kUdp, 5353U);
  const std::array<std::uint8_t, 24> expected = {
      0x00U, 0x15U, 0x16U, 0x00U, 0xFFU, 0x02U, 0x00U, 0x00U,
      0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
      0x00U, 0x00U, 0xABU, 0xCDU, 0x00U, 0x11U, 0x14U, 0xE9U};

  REQUIRE(Options::Kind::kIpV6Multicast == option.Type());
  REQUIRE(24U == option.EncodedSize());
  REQUIRE(expected == Encode<24>(option));
}

TEST_CASE("sd options - IPv4 SD golden bytes", "[sd][options]") {
  const std::array<std::uint8_t, 4> address = {192U, 168U, 0U, 100U};
  const Options option = Options::IpV4Sd(address, TransportProtocol::kUdp, 30490U);
  const std::array<std::uint8_t, 12> expected = {0x00U, 0x09U, 0x24U, 0x00U, 192U,  168U,
                                                 0U,    100U,  0x00U, 0x11U, 0x77U, 0x1AU};

  REQUIRE(Options::Kind::kIpV4Sd == option.Type());
  REQUIRE(12U == option.EncodedSize());
  REQUIRE(expected == Encode<12>(option));
}

TEST_CASE("sd options - IPv6 SD golden bytes", "[sd][options]") {
  const std::array<std::uint8_t, 16> address = {0xFEU, 0x80U, 0x00U, 0x00U, 0x00U, 0x00U,
                                                0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                                                0xABU, 0xCDU, 0xEFU, 0x01U};
  const Options option = Options::IpV6Sd(address, TransportProtocol::kTcp, 30490U);
  const std::array<std::uint8_t, 24> expected = {
      0x00U, 0x15U, 0x26U, 0x00U, 0xFEU, 0x80U, 0x00U, 0x00U,
      0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
      0xABU, 0xCDU, 0xEFU, 0x01U, 0x00U, 0x06U, 0x77U, 0x1AU};

  REQUIRE(Options::Kind::kIpV6Sd == option.Type());
  REQUIRE(24U == option.EncodedSize());
  REQUIRE(expected == Encode<24>(option));
  REQUIRE(30490U == option.Port());
}

TEST_CASE("sd options - short output reports insufficient buffer", "[sd][options]") {
  const Options option = Options::IpV4Endpoint(kIpV4Address, TransportProtocol::kUdp, 30490U);
  std::array<std::uint8_t, 4> out{};
  const auto result = option.EncodeTo(osp::span<std::uint8_t>(out.data(), out.size()));

  REQUIRE_FALSE(result.has_value());
  REQUIRE(ErrorCode::kInsufficientBuffer == result.get_error().code);
  REQUIRE(12U == result.get_error().value);
  REQUIRE(4U == result.get_error().available);
}
