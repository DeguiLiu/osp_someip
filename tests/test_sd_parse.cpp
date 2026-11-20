/**
 * @file test_sd_parse.cpp
 * @brief Zero-copy SD parsing: entries, options, header, and message wiring.
 */

#include "osp/someip/message.hpp"
#include "osp/someip/sd/entry_view.hpp"
#include "osp/someip/sd/header.hpp"
#include "osp/someip/sd/option_view.hpp"
#include "osp/someip/sd_codec.hpp"

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
using osp::someip::sd::OptionType;
using osp::someip::sd::OptionView;
using osp::someip::sd::RebootFlag;
using osp::someip::sd::SdSessionCounter;
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
