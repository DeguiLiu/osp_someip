/**
 * @file test_client.cpp
 * @brief Client core: find repetition, discovery cache, request matching,
 *        timeouts, event delivery and subscription acknowledgements.
 */

#include "osp/someip/client.hpp"

#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::BuildOfferServiceDatagram;
using osp::someip::BuildStopOfferServiceDatagram;
using osp::someip::BuildSubscribeAckDatagram;
using osp::someip::Client;
using osp::someip::Header;
using osp::someip::Ipv4Endpoint;
using osp::someip::Message;
using osp::someip::MessageId;
using osp::someip::MessageType;
using osp::someip::MessageTypeField;
using osp::someip::MessageView;
using osp::someip::OfferServiceRequest;
using osp::someip::OpaquePayload;
using osp::someip::ReturnCode;
using osp::someip::ServiceInstance;
using osp::someip::SubscribeAckRequest;
using osp::someip::sd::RebootFlag;
using osp::someip::sd::SdSession;
using osp::someip::sd::SdSessionCounter;

namespace {

constexpr std::uint32_t kMaxCaptured = 8U;

struct Captured final {
  std::uint8_t bytes[256]{};
  std::uint32_t size{0U};
  Ipv4Endpoint to{};
};

struct Sink final {
  Captured items[kMaxCaptured]{};
  std::uint32_t count{0U};

  void operator()(osp::span<const std::uint8_t> datagram, const Ipv4Endpoint& to) noexcept {
    if (count < kMaxCaptured && datagram.size() <= sizeof(items[0].bytes)) {
      std::memcpy(items[count].bytes, datagram.data(), datagram.size());
      items[count].size = static_cast<std::uint32_t>(datagram.size());
      items[count].to = to;
      ++count;
    }
  }

  const Captured& Last() const noexcept { return items[count - 1U]; }
};

struct Handler final {
  std::uint32_t responses{0U};
  std::uint32_t response_id{0U};
  std::uint32_t response_body{0U};
  std::uint32_t events{0U};
  std::uint8_t event_body[4]{};
  std::uint32_t event_size{0U};
  std::uint32_t timeouts{0U};
  std::uint32_t acks{0U};
  std::uint32_t granted_ttl{0U};
  std::uint16_t ack_event_group{0U};
  std::uint32_t available{0U};
  std::uint32_t gone{0U};
  std::uint32_t reboots{0U};

  void OnResponse(std::uint32_t request_id, MessageView message) noexcept {
    ++responses;
    response_id = request_id;
    response_body = static_cast<std::uint32_t>(message.Body().size());
  }

  void OnEvent(MessageView message) noexcept {
    ++events;
    event_size = static_cast<std::uint32_t>(message.Body().size());
    for (std::uint32_t i = 0U; i < event_size && i < sizeof(event_body); ++i) {
      event_body[i] = message.Body()[i];
    }
  }

  void OnTimeout(std::uint32_t /*request_id*/) noexcept { ++timeouts; }

  void OnSubscribeAck(std::uint16_t /*service_id*/, std::uint16_t /*instance_id*/,
                      std::uint16_t event_group_id, std::uint32_t granted) noexcept {
    ++acks;
    ack_event_group = event_group_id;
    granted_ttl = granted;
  }

  void OnServiceAvailable(osp::someip::ServiceInstance /*service*/) noexcept { ++available; }
  void OnServiceGone(osp::someip::ServiceInstance /*service*/) noexcept { ++gone; }
  void OnSenderRebooted(const osp::someip::Ipv4Endpoint& /*source*/) noexcept { ++reboots; }
};

SdSession Session(std::uint16_t id) noexcept {
  auto counter = SdSessionCounter::Resume(static_cast<std::uint16_t>(id - 1U),
                                          RebootFlag::kContinuous);
  return counter.Next();
}

std::uint32_t BuildOffer(std::uint8_t* out, std::uint32_t ttl, bool stop) noexcept {
  OfferServiceRequest request;
  request.service_id = 0x1234U;
  request.instance_id = 0x0001U;
  request.major_version = 1U;
  request.minor_version = 0U;
  request.ttl = ttl;
  request.local_ip = {192U, 0U, 2U, 1U};
  request.unicast_port = 30501U;
  const auto built = stop ? BuildStopOfferServiceDatagram(osp::make_span(out, 128U), request,
                                                          Session(3U))
                          : BuildOfferServiceDatagram(osp::make_span(out, 128U), request,
                                                      Session(3U));
  return built.has_value() ? built.value() : 0U;
}

std::uint32_t BuildOfferWithSession(std::uint8_t* out, std::uint32_t ttl,
                                    SdSession session) noexcept {
  OfferServiceRequest request;
  request.service_id = 0x1234U;
  request.instance_id = 0x0001U;
  request.major_version = 1U;
  request.ttl = ttl;
  request.local_ip = {192U, 0U, 2U, 1U};
  request.unicast_port = 30501U;
  const auto built =
      BuildOfferServiceDatagram(osp::make_span(out, 128U), request, session);
  return built.has_value() ? built.value() : 0U;
}

SdSession SessionWith(std::uint16_t id, RebootFlag reboot) noexcept {
  auto counter = SdSessionCounter::Resume(static_cast<std::uint16_t>(id - 1U), reboot);
  return counter.Next();
}

}  // namespace

TEST_CASE("client - find repeats at doubling intervals then stops", "[client]") {
  Sink sink;
  Handler handler;
  Client<Sink, Handler, 4U, 2U> client(sink, handler, {{127U, 0U, 0U, 1U}, 30510U});

  REQUIRE(client.StartFind(0x1234U, 0U));
  REQUIRE(1U == sink.count);
  const Ipv4Endpoint multicast{{239U, 255U, 0U, 255U}, 30490U};
  REQUIRE(multicast == sink.Last().to);

  client.Tick(99U);
  REQUIRE(1U == sink.count);
  client.Tick(100U);
  REQUIRE(2U == sink.count);
  client.Tick(300U);
  REQUIRE(3U == sink.count);
  client.Tick(700U);
  REQUIRE(3U == sink.count);
}

TEST_CASE("client - offers populate and stop-offers clear the cache", "[client]") {
  Sink sink;
  Handler handler;
  Client<Sink, Handler, 4U, 2U> client(sink, handler, {{127U, 0U, 0U, 1U}, 30510U});
  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  const Ipv4Endpoint source{{127U, 0U, 0U, 2U}, 30490U};

  std::uint8_t offer[128] = {};
  const std::uint32_t offer_size = BuildOffer(offer, 3U, false);
  REQUIRE(0U != offer_size);
  client.HandleDatagram(osp::make_span(offer, offer_size), source, 0U);
  REQUIRE(1U == client.ServiceCount());
  REQUIRE(1U == handler.available);

  const auto* cached = client.FindCached(service);
  REQUIRE(nullptr != cached);
  const Ipv4Endpoint offered{{192U, 0U, 2U, 1U}, 30501U};
  REQUIRE(offered == cached->endpoint);
  REQUIRE(source == cached->sd_source);

  const std::uint32_t stop_size = BuildOffer(offer, 0U, true);
  REQUIRE(0U != stop_size);
  client.HandleDatagram(osp::make_span(offer, stop_size), source, 10U);
  REQUIRE(0U == client.ServiceCount());
  REQUIRE(nullptr == client.FindCached(service));
  REQUIRE(1U == handler.gone);
}

TEST_CASE("client - request response matching and timeout", "[client]") {
  Sink sink;
  Handler handler;
  Client<Sink, Handler, 4U, 2U> client(sink, handler, {{127U, 0U, 0U, 1U}, 30510U});
  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  const Ipv4Endpoint source{{127U, 0U, 0U, 2U}, 30490U};
  std::uint8_t offer[128] = {};
  client.HandleDatagram(osp::make_span(offer, BuildOffer(offer, 30U, false)), source, 0U);

  const std::uint8_t payload[3] = {0x01U, 0x02U, 0x03U};
  REQUIRE(client.Request(service, 0x0001U, osp::make_span(payload), 100U, 0U));
  REQUIRE(1U == client.PendingCount());
  const Ipv4Endpoint offered{{192U, 0U, 2U, 1U}, 30501U};
  REQUIRE(offered == sink.Last().to);

  const auto request = MessageView::Parse(osp::make_span(sink.Last().bytes, sink.Last().size));
  REQUIRE(request.has_value());
  const std::uint32_t request_id = request.value().Head().RequestId();
  REQUIRE(0U != request_id);

  const std::uint8_t reply_body[2] = {0xAAU, 0xBBU};
  const Header reply_header =
      Header::New(MessageId::FromServiceAndMethod(0x1234U, 0x0001U), request_id, 0x01U, 0x01U,
                  MessageTypeField::New(MessageType::kResponse, false), ReturnCode::Ok(), 2U);
  const Message<OpaquePayload> reply(reply_header, OpaquePayload(osp::make_span(reply_body)));
  std::uint8_t reply_bytes[64] = {};
  const auto reply_size = reply.EncodeTo(osp::make_span(reply_bytes));
  REQUIRE(reply_size.has_value());
  client.HandleDatagram(osp::make_span(reply_bytes, reply_size.value()),
                        {{192U, 0U, 2U, 1U}, 30501U}, 10U);
  REQUIRE(1U == handler.responses);
  REQUIRE(request_id == handler.response_id);
  REQUIRE(2U == handler.response_body);
  REQUIRE(0U == client.PendingCount());

  REQUIRE(client.Request(service, 0x0001U, osp::make_span(payload), 50U, 100U));
  client.Tick(151U);
  REQUIRE(1U == handler.timeouts);
  REQUIRE(0U == client.PendingCount());
}

TEST_CASE("client - notifications reach the handler", "[client]") {
  Sink sink;
  Handler handler;
  Client<Sink, Handler, 4U, 2U> client(sink, handler, {{127U, 0U, 0U, 1U}, 30510U});

  const Header header = Header::NewEvent(0x1234U, 0x8001U, 7U, 0x01U, 1U, 2U);
  const std::uint8_t body[2] = {0x5AU, 0xA5U};
  const Message<OpaquePayload> event(header, OpaquePayload(osp::make_span(body)));
  std::uint8_t bytes[64] = {};
  const auto size = event.EncodeTo(osp::make_span(bytes));
  REQUIRE(size.has_value());

  client.HandleDatagram(osp::make_span(bytes, size.value()), {{192U, 0U, 2U, 1U}, 30501U}, 0U);
  REQUIRE(1U == handler.events);
  REQUIRE(2U == handler.event_size);
  REQUIRE(0x5AU == handler.event_body[0]);
  REQUIRE(0xA5U == handler.event_body[1]);
}

TEST_CASE("client - subscribe sends the local endpoint and reports the ack", "[client]") {
  Sink sink;
  Handler handler;
  const Ipv4Endpoint local{{127U, 0U, 0U, 1U}, 30510U};
  Client<Sink, Handler, 4U, 2U> client(sink, handler, local);
  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  const Ipv4Endpoint source{{127U, 0U, 0U, 2U}, 30490U};
  std::uint8_t offer[128] = {};
  client.HandleDatagram(osp::make_span(offer, BuildOffer(offer, 30U, false)), source, 0U);

  REQUIRE(client.Subscribe(service, 0x0001U, 5U, 0U));
  REQUIRE(source == sink.Last().to);
  const auto message = MessageView::Parse(osp::make_span(sink.Last().bytes, sink.Last().size));
  REQUIRE(message.has_value());
  auto sd = message.value().SdHeader();
  REQUIRE(sd.has_value());
  osp::someip::sd::EntryView entry;
  REQUIRE(sd.value().NextEntry(entry));
  REQUIRE(osp::someip::sd::Entry::Kind::kSubscribe == entry.Kind());
  REQUIRE(5U == entry.Ttl());
  osp::someip::sd::OptionView option;
  REQUIRE(sd.value().OptionAt(entry.IndexFirstOptionsRun(), option));
  REQUIRE(local == (Ipv4Endpoint{option.AddressV4(), option.Port()}));

  SubscribeAckRequest ack;
  ack.service_id = 0x1234U;
  ack.instance_id = 0x0001U;
  ack.event_group_id = 0x0001U;
  ack.major_version = 1U;
  ack.ttl = 5U;
  std::uint8_t ack_bytes[128] = {};
  const auto ack_size =
      BuildSubscribeAckDatagram(osp::make_span(ack_bytes), ack, Session(4U));
  REQUIRE(ack_size.has_value());
  client.HandleDatagram(osp::make_span(ack_bytes, ack_size.value()), source, 10U);
  REQUIRE(1U == handler.acks);
  REQUIRE(0x0001U == handler.ack_event_group);
  REQUIRE(5U == handler.granted_ttl);
}

TEST_CASE("client - service expiry reports gone", "[client]") {
  Sink sink;
  Handler handler;
  Client<Sink, Handler, 4U, 2U> client(sink, handler, {{127U, 0U, 0U, 1U}, 30510U});
  const Ipv4Endpoint source{{127U, 0U, 0U, 2U}, 30490U};

  std::uint8_t offer[128] = {};
  client.HandleDatagram(osp::make_span(offer, BuildOffer(offer, 3U, false)), source, 0U);
  REQUIRE(1U == client.ServiceCount());
  REQUIRE(1U == handler.available);

  client.Tick(3001U);
  REQUIRE(0U == client.ServiceCount());
  REQUIRE(1U == handler.gone);
}

TEST_CASE("client - subscriptions refresh at half ttl and expire without ack", "[client]") {
  Sink sink;
  Handler handler;
  Client<Sink, Handler, 4U, 2U> client(sink, handler, {{127U, 0U, 0U, 1U}, 30510U});
  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  const Ipv4Endpoint source{{127U, 0U, 0U, 2U}, 30490U};
  std::uint8_t offer[128] = {};
  client.HandleDatagram(osp::make_span(offer, BuildOffer(offer, 30U, false)), source, 0U);

  REQUIRE(client.Subscribe(service, 0x0001U, 10U, 0U));
  REQUIRE(1U == client.SubscriptionCount());
  REQUIRE(client.IsSubscribed(service, 0x0001U));
  const std::uint32_t after_subscribe = sink.count;
  REQUIRE(1U == after_subscribe);

  client.Tick(4999U);
  REQUIRE(after_subscribe == sink.count);
  client.Tick(5000U);
  REQUIRE((after_subscribe + 1U) == sink.count);

  SubscribeAckRequest ack;
  ack.service_id = 0x1234U;
  ack.instance_id = 0x0001U;
  ack.event_group_id = 0x0001U;
  ack.major_version = 1U;
  ack.ttl = 10U;
  std::uint8_t ack_bytes[128] = {};
  const auto ack_size = BuildSubscribeAckDatagram(osp::make_span(ack_bytes), ack, Session(4U));
  REQUIRE(ack_size.has_value());
  client.HandleDatagram(osp::make_span(ack_bytes, ack_size.value()), source, 5000U);
  REQUIRE(1U == handler.acks);

  const std::uint32_t after_ack = sink.count;
  client.Tick(9999U);
  REQUIRE(after_ack == sink.count);
  client.Tick(10000U);
  REQUIRE((after_ack + 1U) == sink.count);

  client.Tick(15000U);
  REQUIRE(0U == client.SubscriptionCount());
}

TEST_CASE("client - sender reboot drops the caches", "[client]") {
  Sink sink;
  Handler handler;
  Client<Sink, Handler, 4U, 2U> client(sink, handler, {{127U, 0U, 0U, 1U}, 30510U});
  const Ipv4Endpoint source{{127U, 0U, 0U, 2U}, 30490U};
  std::uint8_t offer[128] = {};

  const std::uint32_t first =
      BuildOfferWithSession(offer, 30U, SessionWith(10U, RebootFlag::kContinuous));
  client.HandleDatagram(osp::make_span(offer, first), source, 0U);
  REQUIRE(1U == client.ServiceCount());

  const std::uint32_t second =
      BuildOfferWithSession(offer, 30U, SessionWith(11U, RebootFlag::kContinuous));
  client.HandleDatagram(osp::make_span(offer, second), source, 1000U);
  REQUIRE(0U == handler.reboots);

  const std::uint32_t rebooted =
      BuildOfferWithSession(offer, 30U, SessionWith(1U, RebootFlag::kRecentlyRebooted));
  client.HandleDatagram(osp::make_span(offer, rebooted), source, 5000U);
  REQUIRE(1U == handler.reboots);
  REQUIRE(1U == handler.gone);
  REQUIRE(2U == handler.available);
  REQUIRE(1U == client.ServiceCount());  // the rebooted offer re-populates the cache
}

TEST_CASE("client - a repeated session id is not a restart", "[client]") {
  Sink sink;
  Handler handler;
  Client<Sink, Handler, 4U, 2U> client(sink, handler, {{127U, 0U, 0U, 1U}, 30510U});
  const Ipv4Endpoint source{{127U, 0U, 0U, 2U}, 30490U};
  std::uint8_t offer[128] = {};

  const std::uint32_t first =
      BuildOfferWithSession(offer, 30U, SessionWith(7U, RebootFlag::kRecentlyRebooted));
  client.HandleDatagram(osp::make_span(offer, first), source, 0U);
  REQUIRE(0U == handler.reboots);

  // The same message arrives again, unicast after multicast; the reboot flag is
  // still set because the peer has not wrapped its session counter yet.
  client.HandleDatagram(osp::make_span(offer, first), source, 10U);
  REQUIRE(0U == handler.reboots);
  REQUIRE(1U == client.ServiceCount());
}

TEST_CASE("client - a quick session drop is not a restart", "[client]") {
  Sink sink;
  Handler handler;
  Client<Sink, Handler, 4U, 2U> client(sink, handler, {{127U, 0U, 0U, 1U}, 30510U});
  const Ipv4Endpoint source{{127U, 0U, 0U, 2U}, 30490U};
  std::uint8_t offer[128] = {};

  // vsomeip numbers the offers it sends in reply to a find from 1 again, so a
  // healthy peer looks like it restarted unless the drop follows real silence.
  const std::uint32_t cyclic =
      BuildOfferWithSession(offer, 30U, SessionWith(10U, RebootFlag::kRecentlyRebooted));
  client.HandleDatagram(osp::make_span(offer, cyclic), source, 0U);

  const std::uint32_t reply =
      BuildOfferWithSession(offer, 30U, SessionWith(1U, RebootFlag::kRecentlyRebooted));
  client.HandleDatagram(osp::make_span(offer, reply), source, 100U);
  REQUIRE(0U == handler.reboots);
  REQUIRE(1U == client.ServiceCount());
}
