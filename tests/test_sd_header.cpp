/**
 * @file test_sd_header.cpp
 * @brief Byte-exact golden snapshots of the SD header.
 *
 * The expected arrays below are hand-derived from the SOME/IP-SD wire layout
 * (flags + reserved, entries_size, entries, options_size, options). They are
 * the oracle: never copy encoder output into a literal here.
 */

#include "osp/someip/sd/header.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::ErrorCode;
using osp::someip::sd::Entry;
using osp::someip::sd::EventGroupEntry;
using osp::someip::sd::Flags;
using osp::someip::sd::Header;
using osp::someip::sd::Options;
using osp::someip::sd::OptionsCount;
using osp::someip::sd::RebootFlag;
using osp::someip::sd::ServiceEntry;
using osp::someip::sd::TransportProtocol;

namespace {

/// @brief Encode @p header and compare every byte against @p expected.
void ExpectBytes(const Header& header, const std::uint8_t* expected, std::size_t size) {
  std::uint8_t buf[64] = {};
  const auto written = header.EncodeTo(osp::make_span(buf));
  REQUIRE(written.has_value());
  REQUIRE(size == written.value());
  REQUIRE(static_cast<std::uint32_t>(size) == header.EncodedSize());
  const bool same = 0 == std::memcmp(buf, expected, size);
  REQUIRE(same);
}

std::array<std::uint8_t, 4U> MakeIpV4(std::uint8_t a, std::uint8_t b, std::uint8_t c,
                                     std::uint8_t d) {
  return std::array<std::uint8_t, 4U>{a, b, c, d};
}

}  // namespace

TEST_CASE("golden - sd header find service", "[sd][header]") {
  // FindService with wildcard instance/version fields, no options.
  const ServiceEntry fields{OptionsCount(1U, 0U), 0x1234U, 0xFFFFU, 0xFFU, 0x00FFFFFFU,
                            0xFFFFFFFFU};
  const Entry entries[1] = {Entry::Find(fields)};
  const Header header(Flags::NewSd(RebootFlag::kRecentlyRebooted), osp::make_span(entries),
                      osp::span<const Options>());

  const std::uint8_t expected[28] = {
      0xC0U, 0x00U, 0x00U, 0x00U,  // flags (reboot|unicast) + 3 reserved bytes
      0x00U, 0x00U, 0x00U, 0x10U,  // entries_size = 16 (1 entry)
      // --- FindService entry (16 bytes) ---
      0x00U,                    // entry type: FindService
      0x00U, 0x00U,             // index_first/second_options_run
      0x10U,                    // options_count: first=1, second=0
      0x12U, 0x34U,             // service_id
      0xFFU, 0xFFU,             // instance_id: wildcard
      0xFFU,                    // major_version: wildcard
      0xFFU, 0xFFU, 0xFFU,      // ttl: wildcard (0x00FFFFFF)
      0xFFU, 0xFFU, 0xFFU, 0xFFU,  // minor_version: wildcard
      0x00U, 0x00U, 0x00U, 0x00U,  // options_size = 0
  };
  ExpectBytes(header, expected, sizeof(expected));
}

TEST_CASE("golden - sd header offer service with ipv4 endpoint", "[sd][header]") {
  const ServiceEntry fields{OptionsCount(1U, 0U), 0x1234U, 0x0001U, 0x01U, 0x00FFFFFFU, 0U};
  const Entry entries[1] = {Entry::Offer(fields)};
  const Options options[1] = {
      Options::IpV4Endpoint(MakeIpV4(192U, 168U, 1U, 10U), TransportProtocol::kUdp, 30509U)};
  const Header header(Flags::NewSd(RebootFlag::kRecentlyRebooted), osp::make_span(entries),
                      osp::make_span(options));

  const std::uint8_t expected[40] = {
      0xC0U, 0x00U, 0x00U, 0x00U,  // flags + reserved
      0x00U, 0x00U, 0x00U, 0x10U,  // entries_size = 16
      // --- OfferService entry (16 bytes) ---
      0x01U,                    // entry type: OfferService
      0x00U, 0x00U,             // index_first/second_options_run
      0x10U,                    // options_count: first=1, second=0
      0x12U, 0x34U,             // service_id
      0x00U, 0x01U,             // instance_id
      0x01U,                    // major_version
      0xFFU, 0xFFU, 0xFFU,      // ttl = 0x00FFFFFF
      0x00U, 0x00U, 0x00U, 0x00U,  // minor_version
      0x00U, 0x00U, 0x00U, 0x0CU,  // options_size = 12
      // --- IPv4Endpoint option (12 bytes) ---
      0x00U, 0x09U,             // length field = 9
      0x04U,                    // option type: IpV4Endpoint
      0x00U,                    // discard flag
      192U, 168U, 1U, 10U,      // ip
      0x00U,                    // reserved
      0x11U,                    // protocol: UDP
      0x77U, 0x2DU,             // port = 30509
  };
  ExpectBytes(header, expected, sizeof(expected));
}

TEST_CASE("golden - sd header stop offer service with load balancing", "[sd][header]") {
  // StopOfferService is an OfferService entry whose TTL is 0.
  const ServiceEntry fields{OptionsCount(1U, 0U), 0xABCDU, 0x0002U, 0x02U, 0U, 0x00000001U};
  const Entry entries[1] = {Entry::StopOffer(fields)};
  const Options options[1] = {Options::LoadBalancing(100U, 200U)};
  const Header header(Flags::NewSd(RebootFlag::kContinuous), osp::make_span(entries),
                      osp::make_span(options));

  const std::uint8_t expected[36] = {
      0x40U, 0x00U, 0x00U, 0x00U,  // flags: unicast only (Continuous reboot)
      0x00U, 0x00U, 0x00U, 0x10U,  // entries_size = 16
      // --- StopOfferService entry (16 bytes) ---
      0x01U,                    // entry type: OfferService
      0x00U, 0x00U,             // index_first/second_options_run
      0x10U,                    // options_count: first=1, second=0
      0xABU, 0xCDU,             // service_id
      0x00U, 0x02U,             // instance_id
      0x02U,                    // major_version
      0x00U, 0x00U, 0x00U,      // ttl = 0 (stop-offer)
      0x00U, 0x00U, 0x00U, 0x01U,  // minor_version
      0x00U, 0x00U, 0x00U, 0x08U,  // options_size = 8
      // --- LoadBalancing option (8 bytes) ---
      0x00U, 0x05U,             // length field = 5
      0x02U,                    // option type: LoadBalancing
      0x00U,                    // discard flag
      0x00U, 0x64U,             // priority = 100
      0x00U, 0xC8U,             // weight = 200
  };
  ExpectBytes(header, expected, sizeof(expected));
}

TEST_CASE("golden - sd header subscribe eventgroup with ipv6 endpoint", "[sd][header]") {
  const EventGroupEntry fields{OptionsCount(1U, 0U), 0x0042U, 0x0001U, 0x01U, 3U, 0U, 1U};
  const Entry entries[1] = {Entry::Subscribe(fields)};
  const std::array<std::uint8_t, 16U> ip{
      0xFEU, 0x80U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
      0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U};
  const Options options[1] = {Options::IpV6Endpoint(ip, TransportProtocol::kTcp, 8080U)};
  const Header header(Flags::NewSd(RebootFlag::kRecentlyRebooted), osp::make_span(entries),
                      osp::make_span(options));

  const std::uint8_t expected[52] = {
      0xC0U, 0x00U, 0x00U, 0x00U,  // flags + reserved
      0x00U, 0x00U, 0x00U, 0x10U,  // entries_size = 16
      // --- Subscribe entry (16 bytes) ---
      0x06U,                    // entry type: Subscribe
      0x00U, 0x00U,             // index_first/second_options_run
      0x10U,                    // options_count: first=1, second=0
      0x00U, 0x42U,             // service_id
      0x00U, 0x01U,             // instance_id
      0x01U,                    // major_version
      0x00U, 0x00U, 0x03U,      // ttl = 3
      0x00U, 0x00U,             // counter = 0
      0x00U, 0x01U,             // event_group_id = 1
      0x00U, 0x00U, 0x00U, 0x18U,  // options_size = 24
      // --- IPv6Endpoint option (24 bytes) ---
      0x00U, 0x15U,             // length field = 21
      0x06U,                    // option type: IpV6Endpoint
      0x00U,                    // discard flag
      0xFEU, 0x80U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
      0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U,  // ip: fe80::1
      0x00U,                    // reserved
      0x06U,                    // protocol: TCP
      0x1FU, 0x90U,             // port = 8080
  };
  ExpectBytes(header, expected, sizeof(expected));
}

TEST_CASE("golden - sd header subscribe ack eventgroup with configuration", "[sd][header]") {
  const EventGroupEntry fields{OptionsCount(1U, 0U), 0xAAAAU, 0x0001U, 0x01U, 0x00FFFFFFU, 0U,
                               0x0010U};
  const Entry entries[1] = {Entry::Ack(fields)};
  const std::uint8_t text[3] = {0x61U, 0x62U, 0x63U};
  const Options options[1] = {Options::Configuration(osp::make_span(text))};
  const Header header(Flags::NewSd(RebootFlag::kContinuous), osp::make_span(entries),
                      osp::make_span(options));

  const std::uint8_t expected[35] = {
      0x40U, 0x00U, 0x00U, 0x00U,  // flags: unicast only
      0x00U, 0x00U, 0x00U, 0x10U,  // entries_size = 16
      // --- SubscribeAck entry (16 bytes) ---
      0x07U,                    // entry type: SubscribeAck
      0x00U, 0x00U,             // index_first/second_options_run
      0x10U,                    // options_count: first=1, second=0
      0xAAU, 0xAAU,             // service_id
      0x00U, 0x01U,             // instance_id
      0x01U,                    // major_version
      0xFFU, 0xFFU, 0xFFU,      // ttl = 0x00FFFFFF
      0x00U, 0x00U,             // counter = 0
      0x00U, 0x10U,             // event_group_id = 0x0010
      0x00U, 0x00U, 0x00U, 0x07U,  // options_size = 7
      // --- Configuration option (7 bytes) ---
      0x00U, 0x04U,             // length field = 4 (1 + string_len 3)
      0x01U,                    // option type: Configuration
      0x00U,                    // discard flag
      0x61U, 0x62U, 0x63U,      // "abc"
  };
  ExpectBytes(header, expected, sizeof(expected));
}

TEST_CASE("sd header - empty payload is fixed part only", "[sd][header]") {
  const Header header(Flags::NewSd(RebootFlag::kContinuous), osp::span<const Entry>(),
                      osp::span<const Options>());
  const std::uint8_t expected[12] = {0x40U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                                     0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
  ExpectBytes(header, expected, sizeof(expected));
}

TEST_CASE("sd header - short buffer reports needed and available", "[sd][header]") {
  const Entry entries[1] = {
      Entry::Offer(ServiceEntry{OptionsCount(1U, 0U), 0x1234U, 0x0001U, 0x01U, 0x00FFFFFFU, 0U})};
  const Options options[1] = {
      Options::IpV4Endpoint(MakeIpV4(192U, 168U, 1U, 10U), TransportProtocol::kUdp, 30509U)};
  const Header header(Flags::NewSd(RebootFlag::kRecentlyRebooted), osp::make_span(entries),
                      osp::make_span(options));

  std::uint8_t buf[39] = {};
  const auto written = header.EncodeTo(osp::make_span(buf));
  REQUIRE_FALSE(written.has_value());
  REQUIRE(ErrorCode::kInsufficientBuffer == written.get_error().code);
  REQUIRE(40U == written.get_error().value);
  REQUIRE(39U == written.get_error().available);
}
