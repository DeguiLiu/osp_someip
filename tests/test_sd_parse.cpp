/**
 * @file test_sd_parse.cpp
 * @brief Zero-copy SD parsing: entries, options, header, and message wiring.
 */

#include "osp/someip/message.hpp"
#include "osp/someip/sd/entry_view.hpp"
#include "osp/someip/sd/header.hpp"
#include "osp/someip/sd/option_view.hpp"
#include "osp/someip/sd_codec.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::BuildOfferServiceDatagram;
using osp::someip::BuildSubscribeAckDatagram;
using osp::someip::BuildSubscribeEventGroupDatagram;
using osp::someip::ErrorCode;
using osp::someip::MessageView;
using osp::someip::OfferServiceRequest;
using osp::someip::SubscribeAckRequest;
using osp::someip::SubscribeEventGroupRequest;
using osp::someip::sd::Entry;
using osp::someip::sd::EntryView;
using osp::someip::sd::Flags;
using osp::someip::sd::Header;
using osp::someip::sd::Options;
using osp::someip::sd::OptionsCount;
using osp::someip::sd::OptionType;
using osp::someip::sd::OptionView;
using osp::someip::sd::RebootFlag;
using osp::someip::sd::SdHeaderView;
using osp::someip::sd::SdSessionCounter;
using osp::someip::sd::ServiceEntry;
using osp::someip::sd::TransportProtocol;

namespace {

constexpr std::uint32_t kSdOffset = 16U;

template <typename T>
void RequireSameBytes(const T& value, osp::span<const std::uint8_t> expected) {
  std::uint8_t buf[600] = {};
  const auto written = value.EncodeTo(osp::make_span(buf));
  REQUIRE(written.has_value());
  REQUIRE(expected.size() == written.value());
  REQUIRE(0 == std::memcmp(buf, expected.data(), expected.size()));
}

/// @brief Encode one offer entry over @p options and parse the payload back.
SdHeaderView ParseOfferWith(std::uint8_t* out, const ServiceEntry& fields,
                            osp::span<const Options> options, EntryView& entry) {
  const Entry entries[1] = {Entry::Offer(fields)};
  const Header header(Flags::NewSd(RebootFlag::kContinuous), osp::make_span(entries), options);
  const auto written = header.EncodeTo(osp::make_span(out, 64U));
  REQUIRE(written.has_value());
  auto parsed = SdHeaderView::Parse(osp::make_span(out, written.value()));
  REQUIRE(parsed.has_value());
  REQUIRE(parsed.value().NextEntry(entry));
  return parsed.value();
}

}  // namespace

TEST_CASE("sd parse - offer datagram round trips through the views", "[sd][parse]") {
  OfferServiceRequest request;
  request.service_id = 0x0042U;
  request.instance_id = 0x0001U;
  request.major_version = 1U;
  request.minor_version = 0U;
  request.ttl = 3U;
  request.local_ip = {192U, 0U, 2U, 1U};
  request.unicast_port = 30501U;

  auto counter =
      SdSessionCounter::Resume(0U, RebootFlag::kContinuous);
  const auto session = counter.Next();
  std::uint8_t datagram[64] = {};
  const auto built = BuildOfferServiceDatagram(osp::make_span(datagram), request, session);
  REQUIRE(built.has_value());

  const auto message = MessageView::Parse(osp::make_span(datagram, built.value()));
  REQUIRE(message.has_value());
  REQUIRE(message.value().IsSd());

  auto sd = message.value().SdHeader();
  REQUIRE(sd.has_value());
  REQUIRE(RebootFlag::kContinuous == sd.value().GetFlags().Reboot());
  REQUIRE(sd.value().GetFlags().Unicast());

  EntryView entry;
  REQUIRE(sd.value().NextEntry(entry));
  REQUIRE(Entry::Kind::kOfferService == entry.Kind());
  REQUIRE(0x0042U == entry.ServiceId());
  REQUIRE(0x0001U == entry.InstanceId());
  REQUIRE(1U == entry.MajorVersion());
  REQUIRE(3U == entry.Ttl());
  REQUIRE(0U == entry.MinorVersion());
  REQUIRE(1U == entry.Count().First());
  REQUIRE_FALSE(sd.value().NextEntry(entry));

  OptionView option;
  REQUIRE(sd.value().NextOption(option));
  REQUIRE(OptionType::kIpV4Endpoint == option.Type());
  REQUIRE(option.AddressV4() == (std::array<std::uint8_t, 4U>{192U, 0U, 2U, 1U}));
  REQUIRE(TransportProtocol::kUdp == option.Protocol());
  REQUIRE(30501U == option.Port());
  REQUIRE_FALSE(sd.value().NextOption(option));

  // Re-encoding the parsed values reproduces the bytes on the wire.
  const auto entry_slice =
      osp::someip::sd::EntryView::Parse(osp::make_span(datagram + kSdOffset + 8U, 16U));
  REQUIRE(entry_slice.has_value());
  RequireSameBytes(entry.ToEntry(), osp::make_span(datagram + kSdOffset + 8U, 16U));
  const auto parsed_option = OptionView::Parse(osp::make_span(datagram + kSdOffset + 28U, 12U));
  REQUIRE(parsed_option.has_value());
  const auto owned_option = parsed_option.value().view.ToOptions();
  REQUIRE(owned_option.has_value());
  RequireSameBytes(owned_option.value(), osp::make_span(datagram + kSdOffset + 28U, 12U));
}

TEST_CASE("sd parse - endpoint options are filtered by transport protocol", "[sd][parse]") {
  const std::array<std::uint8_t, 4U> tcp_ip{192U, 0U, 2U, 1U};
  const std::array<std::uint8_t, 4U> udp_ip{192U, 0U, 2U, 9U};
  const Options tcp = Options::IpV4Endpoint(tcp_ip, TransportProtocol::kTcp, 30501U);
  const Options udp = Options::IpV4Endpoint(udp_ip, TransportProtocol::kUdp, 30502U);
  std::uint8_t payload[64] = {};
  EntryView entry;
  OptionView option;

  // A TCP-only offer has no usable destination for a UDP-only stack.
  const Options tcp_only[1] = {tcp};
  const ServiceEntry single{OptionsCount(1U, 0U), 0x1234U, 0x0001U, 0x01U, 3U, 0U};
  SdHeaderView tcp_header = ParseOfferWith(payload, single, osp::make_span(tcp_only), entry);
  REQUIRE_FALSE(tcp_header.UdpEndpointOption(entry, option));

  // TCP in the first run, UDP in the second: the second run is scanned too.
  const Options mixed[2] = {tcp, udp};
  ServiceEntry two_runs = single;
  two_runs.options_count = OptionsCount(1U, 1U);
  two_runs.index_first_options_run = 0U;
  two_runs.index_second_options_run = 1U;
  SdHeaderView mixed_header = ParseOfferWith(payload, two_runs, osp::make_span(mixed), entry);
  REQUIRE(mixed_header.UdpEndpointOption(entry, option));
  REQUIRE(TransportProtocol::kUdp == option.Protocol());
  REQUIRE(udp_ip == option.AddressV4());
  REQUIRE(30502U == option.Port());
}

TEST_CASE("sd parse - subscribe and ack shapes", "[sd][parse]") {
  SubscribeEventGroupRequest subscribe;
  subscribe.service_id = 0x1234U;
  subscribe.instance_id = 0x0001U;
  subscribe.major_version = 1U;
  subscribe.event_group_id = 0x0001U;
  subscribe.ttl = 5U;
  subscribe.local_ip = {10U, 0U, 0U, 5U};
  subscribe.local_rx_port = 30509U;

  auto counter = SdSessionCounter::Resume(6U, RebootFlag::kContinuous);
  std::uint8_t datagram[64] = {};
  const auto built = BuildSubscribeEventGroupDatagram(osp::make_span(datagram), subscribe,
                                                      counter.Next());
  REQUIRE(built.has_value());
  const auto message = MessageView::Parse(osp::make_span(datagram, built.value()));
  REQUIRE(message.has_value());
  auto sd = message.value().SdHeader();
  REQUIRE(sd.has_value());
  EntryView entry;
  REQUIRE(sd.value().NextEntry(entry));
  REQUIRE(Entry::Kind::kSubscribe == entry.Kind());
  REQUIRE(5U == entry.Ttl());
  REQUIRE(0x0001U == entry.EventGroupId());
  REQUIRE(0U == entry.Counter());

  SubscribeAckRequest ack;
  ack.service_id = 0x0099U;
  ack.instance_id = 2U;
  ack.event_group_id = 0x0005U;
  ack.major_version = 1U;
  ack.ttl = 10U;
  auto ack_counter = SdSessionCounter::Resume(1U, RebootFlag::kContinuous);
  std::uint8_t ack_datagram[64] = {};
  const auto ack_built =
      BuildSubscribeAckDatagram(osp::make_span(ack_datagram), ack, ack_counter.Next());
  REQUIRE(ack_built.has_value());
  const auto ack_message = MessageView::Parse(osp::make_span(ack_datagram, ack_built.value()));
  REQUIRE(ack_message.has_value());
  auto ack_sd = ack_message.value().SdHeader();
  REQUIRE(ack_sd.has_value());
  EntryView ack_entry;
  REQUIRE(ack_sd.value().NextEntry(ack_entry));
  REQUIRE(Entry::Kind::kSubscribeAck == ack_entry.Kind());
  REQUIRE(10U == ack_entry.Ttl());
  OptionView ack_option;
  REQUIRE_FALSE(ack_sd.value().NextOption(ack_option));
}

TEST_CASE("sd parse - unknown entries are skipped", "[sd][parse]") {
  std::uint8_t payload[28] = {};
  payload[0U] = 0x40U;                 // flags: unicast, continuous
  payload[7U] = 0x10U;                 // entries_size = 16
  payload[8U] = 0x22U;                 // unknown entry type
  payload[27U] = 0x00U;                // options_size = 0

  auto sd = osp::someip::sd::SdHeaderView::Parse(osp::make_span(payload));
  REQUIRE(sd.has_value());
  EntryView entry;
  REQUIRE_FALSE(sd.value().NextEntry(entry));
  OptionView option;
  REQUIRE_FALSE(sd.value().NextOption(option));
}

TEST_CASE("sd parse - section sizes are validated", "[sd][parse]") {
  std::uint8_t misaligned[28] = {};
  misaligned[7U] = 0x0FU;  // entries_size = 15, not a multiple of 16
  const auto bad_entries = osp::someip::sd::SdHeaderView::Parse(osp::make_span(misaligned));
  REQUIRE_FALSE(bad_entries.has_value());
  REQUIRE(ErrorCode::kInvalidLength == bad_entries.get_error().code);

  std::uint8_t short_options[28] = {};
  short_options[7U] = 0x10U;   // entries_size = 16
  short_options[27U] = 0x08U;  // options_size claims 8 bytes, none follow
  const auto bad_options = osp::someip::sd::SdHeaderView::Parse(osp::make_span(short_options));
  REQUIRE_FALSE(bad_options.has_value());
  REQUIRE(ErrorCode::kIncomplete == bad_options.get_error().code);
}

TEST_CASE("sd parse - option validation rejects bad length and protocol", "[sd][parse]") {
  const std::uint8_t wrong_length[12] = {0x00U, 0x08U, 0x04U, 0x00U, 10U, 0U,
                                         0U,    1U,    0x00U, 0x11U, 0x77U, 0x1AU};
  const auto bad_len = OptionView::Parse(osp::make_span(wrong_length));
  REQUIRE_FALSE(bad_len.has_value());
  REQUIRE(ErrorCode::kInvalidOptionLength == bad_len.get_error().code);
  REQUIRE(9U == bad_len.get_error().value);
  REQUIRE(8U == bad_len.get_error().available);

  const std::uint8_t bad_protocol[12] = {0x00U, 0x09U, 0x04U, 0x00U, 10U, 0U,
                                         0U,    1U,    0x00U, 0x05U, 0x77U, 0x1AU};
  const auto bad_proto = OptionView::Parse(osp::make_span(bad_protocol));
  REQUIRE_FALSE(bad_proto.has_value());
  REQUIRE(ErrorCode::kInvalidOptionProtocol == bad_proto.get_error().code);
  REQUIRE(0x05U == bad_proto.get_error().value);
}

TEST_CASE("sd parse - unknown option type is kept but not owned", "[sd][parse]") {
  const std::uint8_t unknown[4] = {0x00U, 0x01U, 0x50U, 0x00U};
  const auto parsed = OptionView::Parse(osp::make_span(unknown));
  REQUIRE(parsed.has_value());
  REQUIRE_FALSE(parsed.value().view.IsKnown());
  REQUIRE(parsed.value().rest.empty());
  const auto owned = parsed.value().view.ToOptions();
  REQUIRE_FALSE(owned.has_value());
  REQUIRE(ErrorCode::kInvalidOptionType == owned.get_error().code);
}

TEST_CASE("sd parse - entry views report short and unknown inputs", "[sd][parse]") {
  const std::uint8_t short_entry[15] = {};
  const auto short_view = EntryView::Parse(osp::make_span(short_entry));
  REQUIRE_FALSE(short_view.has_value());
  REQUIRE(ErrorCode::kIncomplete == short_view.get_error().code);

  const std::uint8_t unknown_entry[16] = {0x22U};
  const auto unknown_view = EntryView::Parse(osp::make_span(unknown_entry));
  REQUIRE_FALSE(unknown_view.has_value());
  REQUIRE(ErrorCode::kInvalidEntryType == unknown_view.get_error().code);
  REQUIRE(0x22U == unknown_view.get_error().value);
}

TEST_CASE("sd parse - non-SD message has no SD header", "[sd][parse]") {
  std::uint8_t buf[16] = {};
  const auto header = osp::someip::Header::New(
      osp::someip::MessageId::FromServiceAndMethod(0x005BU, 0x0001U), 1U, 0x01U, 0x01U,
      osp::someip::MessageTypeField::New(osp::someip::MessageType::kRequest, false),
      osp::someip::ReturnCode::Ok(), 0U);
  const auto written = header.EncodeTo(osp::make_span(buf));
  REQUIRE(written.has_value());
  const auto message = MessageView::Parse(osp::make_span(buf));
  REQUIRE(message.has_value());
  const auto sd = message.value().SdHeader();
  REQUIRE_FALSE(sd.has_value());
  REQUIRE(ErrorCode::kNotSdMessage == sd.get_error().code);
}

namespace {

template <typename Check>
void RoundTripThroughView(const osp::someip::sd::Options& option, Check check) {
  std::uint8_t encoded[64] = {};
  const auto written = option.EncodeTo(osp::make_span(encoded));
  REQUIRE(written.has_value());
  const auto parsed = OptionView::Parse(osp::span<const std::uint8_t>(encoded, written.value()));
  REQUIRE(parsed.has_value());
  const OptionView view = parsed.value().view;
  REQUIRE(view.IsKnown());
  REQUIRE(static_cast<std::uint8_t>(option.Type()) == view.RawType());
  REQUIRE(written.value() == view.WireSize());
  check(view);

  const auto rebuilt = view.ToOptions();
  REQUIRE(rebuilt.has_value());
  std::uint8_t again[64] = {};
  const auto written_again = rebuilt.value().EncodeTo(osp::make_span(again));
  REQUIRE(written_again.has_value());
  REQUIRE(written.value() == written_again.value());
  REQUIRE(0 == std::memcmp(encoded, again, written.value()));
}

constexpr std::array<std::uint8_t, 16U> kIpV6{0x20U, 0x01U, 0x0DU, 0xB8U, 0U, 0U, 0U, 0U,
                                              0U, 0U, 0U, 0U, 0U, 0U, 0U, 0x01U};

}  // namespace

TEST_CASE("sd parse - every option kind round trips through the view", "[sd][parse]") {
  using osp::someip::sd::Options;
  using osp::someip::sd::OptionType;
  using osp::someip::sd::TransportProtocol;
  const std::array<std::uint8_t, 4U> ipv4{10U, 0U, 0U, 1U};
  const std::array<std::uint8_t, 3U> text{'a', 'b', 'c'};

  RoundTripThroughView(Options::Configuration(osp::span<const std::uint8_t>(text.data(), text.size())),
                       [&text](const OptionView& view) {
                         REQUIRE(text.size() == view.Configuration().size());
                         REQUIRE(0 == std::memcmp(text.data(), view.Configuration().data(),
                                                  text.size()));
                       });
  RoundTripThroughView(Options::LoadBalancing(0x0102U, 0x0304U),
                       [](const OptionView& view) {
                         REQUIRE(0x0102U == view.Priority());
                         REQUIRE(0x0304U == view.Weight());
                       });
  RoundTripThroughView(Options::IpV4Endpoint(ipv4, TransportProtocol::kUdp, 30490U),
                       [&ipv4](const OptionView& view) {
                         REQUIRE(OptionType::kIpV4Endpoint == view.Type());
                         REQUIRE(ipv4 == view.AddressV4());
                         REQUIRE(TransportProtocol::kUdp == view.Protocol());
                         REQUIRE(30490U == view.Port());
                       });
  RoundTripThroughView(Options::IpV6Endpoint(kIpV6, TransportProtocol::kTcp, 443U),
                       [](const OptionView& view) {
                         REQUIRE(OptionType::kIpV6Endpoint == view.Type());
                         REQUIRE(kIpV6 == view.AddressV6());
                         REQUIRE(TransportProtocol::kTcp == view.Protocol());
                         REQUIRE(443U == view.Port());
                       });
  RoundTripThroughView(Options::IpV4Multicast(ipv4, TransportProtocol::kUdp, 30491U),
                       [&ipv4](const OptionView& view) {
                         REQUIRE(OptionType::kIpV4Multicast == view.Type());
                         REQUIRE(ipv4 == view.AddressV4());
                         REQUIRE(30491U == view.Port());
                       });
  RoundTripThroughView(Options::IpV6Multicast(kIpV6, TransportProtocol::kUdp, 5353U),
                       [](const OptionView& view) {
                         REQUIRE(OptionType::kIpV6Multicast == view.Type());
                         REQUIRE(kIpV6 == view.AddressV6());
                         REQUIRE(5353U == view.Port());
                       });
  RoundTripThroughView(Options::IpV4Sd(ipv4, TransportProtocol::kTcp, 30492U),
                       [&ipv4](const OptionView& view) {
                         REQUIRE(OptionType::kIpV4Sd == view.Type());
                         REQUIRE(ipv4 == view.AddressV4());
                         REQUIRE(TransportProtocol::kTcp == view.Protocol());
                         REQUIRE(30492U == view.Port());
                       });
  RoundTripThroughView(Options::IpV6Sd(kIpV6, TransportProtocol::kUdp, 30493U),
                       [](const OptionView& view) {
                         REQUIRE(OptionType::kIpV6Sd == view.Type());
                         REQUIRE(kIpV6 == view.AddressV6());
                         REQUIRE(30493U == view.Port());
                       });
}

TEST_CASE("sd parse - event group entries round trip through the view", "[sd][parse]") {
  using osp::someip::sd::EventGroupEntry;
  using osp::someip::sd::OptionsCount;

  EventGroupEntry payload;
  payload.options_count = OptionsCount(1U, 0U);
  payload.service_id = 0x1234U;
  payload.instance_id = 0x0001U;
  payload.major_version = 1U;
  payload.ttl = 5U;
  payload.counter = 3U;
  payload.event_group_id = 0x0002U;
  payload.index_first_options_run = 0U;
  payload.index_second_options_run = 0U;

  EventGroupEntry rejected = payload;
  rejected.ttl = 0U;  // a zero TTL is what separates a Nack from an Ack on the wire
  const Entry entries[3] = {Entry::Subscribe(payload), Entry::Ack(payload),
                            Entry::Nack(rejected)};
  for (const Entry& entry : entries) {
    std::uint8_t encoded[osp::someip::sd::kEntrySize] = {};
    const auto written = entry.EncodeTo(osp::make_span(encoded));
    REQUIRE(written.has_value());
    const auto view = EntryView::Parse(osp::span<const std::uint8_t>(encoded));
    REQUIRE(view.has_value());
    REQUIRE(static_cast<int>(entry.Type()) == static_cast<int>(view.value().Kind()));
    REQUIRE(payload.service_id == view.value().ServiceId());
    REQUIRE(payload.event_group_id == view.value().EventGroupId());
    REQUIRE(payload.counter == view.value().Counter());

    INFO("wire type " << static_cast<int>(encoded[0U]));
    const Entry rebuilt = view.value().ToEntry();
    std::uint8_t again[osp::someip::sd::kEntrySize] = {};
    const auto written_again = rebuilt.EncodeTo(osp::make_span(again));
    REQUIRE(written_again.has_value());
    REQUIRE(0 == std::memcmp(encoded, again, sizeof(encoded)));
  }
}

TEST_CASE("sd header - short buffers are rejected", "[sd][header]") {
  using osp::someip::sd::Header;
  using osp::someip::sd::Flags;
  using osp::someip::sd::Options;
  using osp::someip::sd::RebootFlag;
  osp::someip::sd::ServiceEntry service;
  service.options_count = osp::someip::sd::OptionsCount(1U, 0U);
  service.service_id = 0x1234U;
  service.instance_id = 0x0001U;
  service.major_version = 1U;
  service.ttl = 3U;
  const Entry entries[1] = {Entry::Offer(service)};
  const std::array<std::uint8_t, 2U> text{'o', 'k'};
  const Options options[1] = {
      Options::Configuration(osp::span<const std::uint8_t>(text.data(), text.size()))};
  const Header header(Flags::NewSd(RebootFlag::kContinuous), osp::make_span(entries),
                      osp::make_span(options));
  std::uint8_t buffer[64] = {};
  REQUIRE(header.EncodedSize() <= sizeof(buffer));

  const auto tiny = header.EncodeTo(osp::make_span(buffer, 4U));
  REQUIRE(!tiny.has_value());
  REQUIRE(osp::someip::ErrorCode::kInsufficientBuffer == tiny.get_error().code);

  const auto almost = header.EncodeTo(osp::make_span(buffer, header.EncodedSize() - 1U));
  REQUIRE(!almost.has_value());
  REQUIRE(osp::someip::ErrorCode::kInsufficientBuffer == almost.get_error().code);

  REQUIRE(header.EncodeTo(osp::make_span(buffer)).has_value());
}

TEST_CASE("sd codec - invalid TTL and short buffers are rejected", "[sd][codec]") {
  using osp::someip::ErrorCode;
  const auto session = osp::someip::sd::SdSessionCounter::New().Next();
  std::uint8_t buffer[128] = {};

  osp::someip::OfferServiceRequest offer;
  offer.service_id = 0x1234U;
  offer.instance_id = 0x0001U;
  offer.major_version = 1U;
  offer.ttl = osp::someip::sd::kMaxTtl + 1U;
  offer.local_ip = {127U, 0U, 0U, 1U};
  offer.unicast_port = 30509U;
  const auto bad_offer =
      osp::someip::BuildOfferServiceDatagram(osp::make_span(buffer), offer, session);
  REQUIRE(!bad_offer.has_value());
  REQUIRE(ErrorCode::kInvalidTtl == bad_offer.get_error().code);

  osp::someip::SubscribeEventGroupRequest subscribe;
  subscribe.service_id = 0x1234U;
  subscribe.instance_id = 0x0001U;
  subscribe.major_version = 1U;
  subscribe.event_group_id = 0x0001U;
  subscribe.ttl = osp::someip::sd::kMaxTtl + 1U;
  subscribe.local_ip = {127U, 0U, 0U, 1U};
  subscribe.local_rx_port = 30510U;
  const auto bad_subscribe = osp::someip::BuildSubscribeEventGroupDatagram(
      osp::make_span(buffer), subscribe, session);
  REQUIRE(!bad_subscribe.has_value());
  REQUIRE(ErrorCode::kInvalidTtl == bad_subscribe.get_error().code);

  osp::someip::SubscribeAckRequest ack;
  ack.service_id = 0x1234U;
  ack.instance_id = 0x0001U;
  ack.major_version = 1U;
  ack.event_group_id = 0x0001U;
  ack.ttl = osp::someip::sd::kMaxTtl + 1U;
  const auto bad_ack =
      osp::someip::BuildSubscribeAckDatagram(osp::make_span(buffer), ack, session);
  REQUIRE(!bad_ack.has_value());
  REQUIRE(ErrorCode::kInvalidTtl == bad_ack.get_error().code);

  const auto small =
      osp::someip::BuildFindServiceDatagram(osp::make_span(buffer, 8U), 0x1234U, session);
  REQUIRE(!small.has_value());
  REQUIRE(ErrorCode::kInsufficientBuffer == small.get_error().code);
}
