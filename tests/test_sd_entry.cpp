/**
 * @file test_sd_entry.cpp
 * @brief Unit tests for the SD entry value types and their 16-byte encoding.
 *
 * Every expected byte array is derived by hand from the SOME/IP-SD entry
 * layout: type, first and second option run indices, packed option counts,
 * service and instance IDs, major version, 24-bit TTL, then the minor version
 * (service entries) or the counter plus event group ID (event group entries).
 * The literals are the oracle: a mismatch is a wire bug, never a test to be
 * relaxed to match encoder output.
 */

#include "osp/someip/sd/entry.hpp"
#include "osp/someip/sd/entry_view.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::ErrorCode;
using osp::someip::sd::Entry;
using osp::someip::sd::EventGroupEntry;
using osp::someip::sd::OptionsCount;
using osp::someip::sd::ServiceEntry;

namespace {

/// @brief Encode @p entry and require the exact 16 hand-written bytes.
void RequireEncodesTo(const Entry& entry, const std::uint8_t (&expected)[16]) {
  std::uint8_t buffer[16] = {};
  const auto written = entry.EncodeTo(osp::make_span(buffer));
  REQUIRE(written.has_value());
  REQUIRE(16U == written.value());
  REQUIRE(0 == std::memcmp(buffer, expected, sizeof(expected)));
}

ServiceEntry MakeOfferEntry() {
  ServiceEntry entry{};
  entry.options_count = OptionsCount(1U, 0U);
  entry.service_id = 0x1234U;
  entry.instance_id = 0x0001U;
  entry.major_version = 0x01U;
  entry.ttl = 0x00FFFFFFU;
  entry.minor_version = 0x00000007U;
  return entry;
}

EventGroupEntry MakeEventGroupEntry() {
  EventGroupEntry entry{};
  entry.options_count = OptionsCount(1U, 0U);
  entry.service_id = 0x0042U;
  entry.instance_id = 0x0001U;
  entry.major_version = 0x01U;
  entry.ttl = 3U;
  entry.counter = 0U;
  entry.event_group_id = 0x0001U;
  return entry;
}

}  // namespace

TEST_CASE("sd entry - find service golden bytes", "[sd][entry]") {
  // An option count of (1, 0) packs to 0x10; the instance, major version, TTL
  // and minor version are the wildcards of ServiceEntry::Find.
  const Entry entry = Entry::Find(ServiceEntry::Find(0x1234U));
  const std::uint8_t expected[16] = {
      0x00U,                    // entry type: find service
      0x00U, 0x00U,             // first / second option run index
      0x10U,                    // options count: first = 1, second = 0
      0x12U, 0x34U,             // service_id
      0xFFU, 0xFFU,             // instance_id: any instance
      0xFFU,                    // major_version: any version
      0xFFU, 0xFFU, 0xFFU,      // ttl: 0x00FFFFFF
      0xFFU, 0xFFU, 0xFFU, 0xFFU,  // minor_version: any minor version
  };
  RequireEncodesTo(entry, expected);
}

TEST_CASE("sd entry - offer service golden bytes", "[sd][entry]") {
  ServiceEntry service = MakeOfferEntry();
  service.options_count = OptionsCount(2U, 1U);
  const Entry entry = Entry::Offer(service);
  const std::uint8_t expected[16] = {
      0x01U,                    // entry type: offer service
      0x00U, 0x00U,             // first / second option run index
      0x21U,                    // options count: first = 2, second = 1
      0x12U, 0x34U,             // service_id
      0x00U, 0x01U,             // instance_id
      0x01U,                    // major_version
      0xFFU, 0xFFU, 0xFFU,      // ttl: 0x00FFFFFF
      0x00U, 0x00U, 0x00U, 0x07U,  // minor_version
  };
  RequireEncodesTo(entry, expected);
}

TEST_CASE("sd entry - stop offer forces a zero ttl", "[sd][entry]") {
  ServiceEntry service = MakeOfferEntry();
  service.service_id = 0xABCDU;
  service.instance_id = 0x0002U;
  service.major_version = 0x02U;
  // The caller's TTL is ignored: a stop offer is an offer with a TTL of 0.
  service.ttl = 0x00123456U;
  service.minor_version = 0x00000001U;
  const Entry entry = Entry::StopOffer(service);
  const std::uint8_t expected[16] = {
      0x01U,                    // entry type: offer service
      0x00U, 0x00U,             // first / second option run index
      0x10U,                    // options count: first = 1, second = 0
      0xABU, 0xCDU,             // service_id
      0x00U, 0x02U,             // instance_id
      0x02U,                    // major_version
      0x00U, 0x00U, 0x00U,      // ttl: 0 marks the stop offer
      0x00U, 0x00U, 0x00U, 0x01U,  // minor_version
  };
  RequireEncodesTo(entry, expected);
}

TEST_CASE("sd entry - subscribe golden bytes", "[sd][entry]") {
  const Entry entry = Entry::Subscribe(MakeEventGroupEntry());
  const std::uint8_t expected[16] = {
      0x06U,                    // entry type: subscribe event group
      0x00U, 0x00U,             // first / second option run index
      0x10U,                    // options count: first = 1, second = 0
      0x00U, 0x42U,             // service_id
      0x00U, 0x01U,             // instance_id
      0x01U,                    // major_version
      0x00U, 0x00U, 0x03U,      // ttl: 3 seconds
      0x00U, 0x00U,             // counter
      0x00U, 0x01U,             // event_group_id
  };
  RequireEncodesTo(entry, expected);
}

TEST_CASE("sd entry - subscribe ack carries the granted ttl", "[sd][entry]") {
  EventGroupEntry group = MakeEventGroupEntry();
  group.service_id = 0xAAAAU;
  // An acknowledgement grants the caller's TTL; 0xFFFFFF means until reboot.
  group.ttl = 0x00FFFFFFU;
  group.event_group_id = 0x0010U;
  const Entry entry = Entry::Ack(group);
  const std::uint8_t expected[16] = {
      0x07U,                    // entry type: subscribe event group ack
      0x00U, 0x00U,             // first / second option run index
      0x10U,                    // options count: first = 1, second = 0
      0xAAU, 0xAAU,             // service_id
      0x00U, 0x01U,             // instance_id
      0x01U,                    // major_version
      0xFFU, 0xFFU, 0xFFU,      // ttl: 0x00FFFFFF, valid until reboot
      0x00U, 0x00U,             // counter
      0x00U, 0x10U,             // event_group_id
  };
  RequireEncodesTo(entry, expected);
}

TEST_CASE("sd entry - subscribe nack forces a zero ttl", "[sd][entry]") {
  EventGroupEntry group{};
  group.options_count = OptionsCount(0U, 0U);
  group.service_id = 0x0055U;
  group.instance_id = 0x0033U;
  group.major_version = 0x04U;
  group.ttl = 0x0000FFFFU;  // ignored: a rejection grants nothing
  group.counter = 0x0003U;
  group.event_group_id = 0x0007U;
  const Entry entry = Entry::Nack(group);
  const std::uint8_t expected[16] = {
      0x07U,                    // entry type: subscribe event group ack
      0x00U, 0x00U,             // first / second option run index
      0x00U,                    // options count: first = 0, second = 0
      0x00U, 0x55U,             // service_id
      0x00U, 0x33U,             // instance_id
      0x04U,                    // major_version
      0x00U, 0x00U, 0x00U,      // ttl: 0 marks the rejection
      0x00U, 0x03U,             // counter
      0x00U, 0x07U,             // event_group_id
  };
  RequireEncodesTo(entry, expected);
}

TEST_CASE("sd entry - option counts pack into one nibble pair", "[sd][entry]") {
  const OptionsCount unpacked = OptionsCount::FromByte(0x10U);
  REQUIRE(1U == unpacked.First());
  REQUIRE(0U == unpacked.Second());
  REQUIRE(0x10U == unpacked.Value());

  const OptionsCount wide = OptionsCount::FromByte(0xF5U);
  REQUIRE(15U == wide.First());
  REQUIRE(5U == wide.Second());
  REQUIRE(0xF5U == wide.Value());

  REQUIRE(0x23U == OptionsCount(2U, 3U).Value());
  REQUIRE(OptionsCount(2U, 3U) == OptionsCount::FromByte(0x23U));
}

TEST_CASE("sd entry - find helper carries the wildcard fields", "[sd][entry]") {
  const ServiceEntry entry = ServiceEntry::Find(0x005BU);
  REQUIRE(0x005BU == entry.service_id);
  REQUIRE(0xFFFFU == entry.instance_id);
  REQUIRE(0xFFU == entry.major_version);
  REQUIRE(0x00FFFFFFU == entry.ttl);
  REQUIRE(0xFFFFFFFFU == entry.minor_version);
  REQUIRE(1U == entry.options_count.First());
  REQUIRE(0U == entry.options_count.Second());
}

TEST_CASE("sd entry - kinds map to the right wire type byte", "[sd][entry]") {
  const Entry find = Entry::Find(ServiceEntry::Find(0x1234U));
  REQUIRE(Entry::Kind::kFindService == find.Type());
  REQUIRE(find.IsService());

  const Entry offer = Entry::Offer(MakeOfferEntry());
  REQUIRE(Entry::Kind::kOfferService == offer.Type());
  REQUIRE(offer.IsService());

  const Entry stop_offer = Entry::StopOffer(MakeOfferEntry());
  REQUIRE(Entry::Kind::kStopOfferService == stop_offer.Type());
  REQUIRE(stop_offer.IsService());

  const Entry subscribe = Entry::Subscribe(MakeEventGroupEntry());
  REQUIRE(Entry::Kind::kSubscribe == subscribe.Type());
  REQUIRE_FALSE(subscribe.IsService());

  const Entry ack = Entry::Ack(MakeEventGroupEntry());
  REQUIRE(Entry::Kind::kSubscribeAck == ack.Type());
  REQUIRE_FALSE(ack.IsService());

  const Entry nack = Entry::Nack(MakeEventGroupEntry());
  REQUIRE(Entry::Kind::kSubscribeNack == nack.Type());
  REQUIRE_FALSE(nack.IsService());
}

TEST_CASE("sd entry - factories keep the caller fields", "[sd][entry]") {
  const ServiceEntry service = MakeOfferEntry();
  const Entry offer = Entry::Offer(service);
  REQUIRE(0x1234U == offer.Service().service_id);
  REQUIRE(0x0001U == offer.Service().instance_id);
  REQUIRE(0x01U == offer.Service().major_version);
  REQUIRE(0x00FFFFFFU == offer.Service().ttl);
  REQUIRE(0x00000007U == offer.Service().minor_version);
  REQUIRE(service.options_count == offer.Service().options_count);

  const EventGroupEntry group = MakeEventGroupEntry();
  const Entry subscribe = Entry::Subscribe(group);
  REQUIRE(0x0042U == subscribe.EventGroup().service_id);
  REQUIRE(0x0001U == subscribe.EventGroup().instance_id);
  REQUIRE(0x01U == subscribe.EventGroup().major_version);
  REQUIRE(3U == subscribe.EventGroup().ttl);
  REQUIRE(0x0001U == subscribe.EventGroup().event_group_id);
  REQUIRE(group.options_count == subscribe.EventGroup().options_count);
}

TEST_CASE("sd entry - every kind encodes in sixteen bytes", "[sd][entry]") {
  const Entry entries[] = {
      Entry::Find(ServiceEntry::Find(0x1234U)),
      Entry::Offer(MakeOfferEntry()),
      Entry::StopOffer(MakeOfferEntry()),
      Entry::Subscribe(MakeEventGroupEntry()),
      Entry::Ack(MakeEventGroupEntry()),
      Entry::Nack(MakeEventGroupEntry()),
  };
  for (const Entry& entry : entries) {
    REQUIRE(16U == entry.EncodedSize());
  }
}

TEST_CASE("sd entry - short output buffer reports the required size", "[sd][entry]") {
  const Entry entry = Entry::Find(ServiceEntry::Find(0x1234U));

  std::uint8_t short_buffer[15] = {};
  const auto short_written = entry.EncodeTo(osp::make_span(short_buffer));
  REQUIRE_FALSE(short_written.has_value());
  REQUIRE(ErrorCode::kInsufficientBuffer == short_written.get_error().code);
  REQUIRE(16U == short_written.get_error().value);
  REQUIRE(15U == short_written.get_error().available);

  const auto empty_written = entry.EncodeTo(osp::span<std::uint8_t>());
  REQUIRE_FALSE(empty_written.has_value());
  REQUIRE(ErrorCode::kInsufficientBuffer == empty_written.get_error().code);
  REQUIRE(16U == empty_written.get_error().value);
  REQUIRE(0U == empty_written.get_error().available);
}

TEST_CASE("sd entry - non-zero option run indices round trip", "[sd][entry]") {
  ServiceEntry service{};
  service.options_count = OptionsCount(1U, 2U);
  service.service_id = 0x1234U;
  service.index_first_options_run = 3U;
  service.index_second_options_run = 7U;
  const Entry entry = Entry::Offer(service);

  const std::uint8_t expected[16] = {
      0x01U, 0x03U, 0x07U, 0x12U,  // offer, run indices 3 and 7, counts 1/2
      0x12U, 0x34U, 0x00U, 0x00U,  // service 0x1234, instance 0
      0x00U, 0x00U, 0x00U, 0x00U,  // major 0, ttl 0
      0x00U, 0x00U, 0x00U, 0x00U,  // minor version 0
  };
  RequireEncodesTo(entry, expected);

  const auto view = osp::someip::sd::EntryView::Parse(osp::make_span(expected));
  REQUIRE(view.has_value());
  REQUIRE(3U == view.value().IndexFirstOptionsRun());
  REQUIRE(7U == view.value().IndexSecondOptionsRun());
  RequireEncodesTo(view.value().ToEntry(), expected);
}
