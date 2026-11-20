/**
 * @file test_server.cpp
 * @brief Server behaviour: offers, find replies, subscriptions, publishing.
 */

#include "osp/someip/message.hpp"
#include "osp/someip/sd/header.hpp"
#include "osp/someip/sd_codec.hpp"
#include "osp/someip/server.hpp"

#include <cstdint>
#include <cstring>
#include <array>

#include <catch2/catch_test_macros.hpp>

using osp::someip::BuildFindServiceDatagram;
using osp::someip::BuildOfferServiceDatagram;
using osp::someip::BuildStopOfferServiceDatagram;
using osp::someip::BuildSubscribeEventGroupDatagram;
using osp::someip::Header;
using osp::someip::IncomingRequest;
using osp::someip::Ipv4Endpoint;
using osp::someip::Message;
using osp::someip::MessageId;
using osp::someip::MessageView;
using osp::someip::MessageType;
using osp::someip::MessageTypeField;
using osp::someip::OpaquePayload;
using osp::someip::OfferServiceRequest;
using osp::someip::RequestReply;
using osp::someip::ReturnCode;
using osp::someip::Server;
using osp::someip::ServiceInstance;
using osp::someip::SubscribeEventGroupRequest;
using osp::someip::sd::Entry;
using osp::someip::sd::EntryView;
using osp::someip::sd::RebootFlag;
using osp::someip::sd::SdSession;
using osp::someip::sd::SdSessionCounter;

namespace {

constexpr std::uint32_t kMaxCaptured = 16U;
constexpr std::uint32_t kMaxCaptureBytes = 256U;

struct Captured final {
  std::uint8_t bytes[kMaxCaptureBytes]{};
  std::uint32_t size{0U};
  Ipv4Endpoint to{};
};

struct Capture final {
  mutable Captured items[kMaxCaptured]{};
  mutable std::uint32_t count{0U};

  void operator()(osp::span<const std::uint8_t> datagram,
                  const Ipv4Endpoint& to) const noexcept {
    if (count < kMaxCaptured && datagram.size() <= kMaxCaptureBytes) {
      std::memcpy(items[count].bytes, datagram.data(), datagram.size());
      items[count].size = static_cast<std::uint32_t>(datagram.size());
      items[count].to = to;
      ++count;
    }
  }

  const Captured& Last() const noexcept { return items[count - 1U]; }
};

SdSession Session(std::uint16_t id) noexcept {
  auto counter = SdSessionCounter::Resume(static_cast<std::uint16_t>(id - 1U),
                                          RebootFlag::kContinuous);
  return counter.Next();
}

std::uint32_t BuildSubscribe(std::uint8_t* out, std::uint16_t service_id,
                             std::uint16_t event_group_id, std::uint32_t ttl,
                             const std::array<std::uint8_t, 4U>& ip,
                             std::uint16_t port) noexcept {
  SubscribeEventGroupRequest request;
  request.service_id = service_id;
  request.instance_id = 0x0001U;
  request.major_version = 1U;
  request.event_group_id = event_group_id;
  request.ttl = ttl;
  request.local_ip = ip;
  request.local_rx_port = port;
  const auto built =
      BuildSubscribeEventGroupDatagram(osp::make_span(out, 128U), request, Session(9U));
  return built.has_value() ? built.value() : 0U;
}

}  // namespace

TEST_CASE("server - offer then find replies with an offer", "[server]") {
  Capture capture;
  Server<Capture, 4U, 4U> server(capture);
  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  const Ipv4Endpoint endpoint{{127U, 0U, 0U, 1U}, 30509U};

  REQUIRE(server.Offer(service, endpoint, 3U, 0U));
  REQUIRE(1U == server.OfferedCount());
  REQUIRE(1U == capture.count);

  std::uint8_t find[64] = {};
  const auto find_size = BuildFindServiceDatagram(osp::make_span(find), 0x1234U, Session(5U));
  REQUIRE(find_size.has_value());
  const Ipv4Endpoint client{{127U, 0U, 0U, 2U}, 30490U};
  server.HandleDatagram(osp::make_span(find, find_size.value()), client, 10U);
  REQUIRE(2U == capture.count);
  REQUIRE(client == capture.Last().to);

  const auto message = MessageView::Parse(osp::make_span(capture.Last().bytes,
                                                         capture.Last().size));
  REQUIRE(message.has_value());
  auto sd = message.value().SdHeader();
  REQUIRE(sd.has_value());
  EntryView entry;
  REQUIRE(sd.value().NextEntry(entry));
  REQUIRE(Entry::Kind::kOfferService == entry.Kind());
  REQUIRE(0x1234U == entry.ServiceId());
  REQUIRE(0x0001U == entry.InstanceId());
  REQUIRE(3U == entry.Ttl());
}

TEST_CASE("server - subscribe is acknowledged and publish reaches the subscriber", "[server]") {
  Capture capture;
  Server<Capture, 4U, 4U> server(capture);
  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  const Ipv4Endpoint endpoint{{127U, 0U, 0U, 1U}, 30509U};
  REQUIRE(server.Offer(service, endpoint, 30U, 0U));

  std::uint8_t subscribe[128] = {};
  const Ipv4Endpoint client{{127U, 0U, 0U, 2U}, 30490U};
  const std::uint32_t subscribe_size =
      BuildSubscribe(subscribe, 0x1234U, 0x0001U, 10U, {127U, 0U, 0U, 2U}, 30510U);
  REQUIRE(0U != subscribe_size);
  server.HandleDatagram(osp::make_span(subscribe, subscribe_size), client, 100U);

  REQUIRE(1U == server.SubscriberCount());
  REQUIRE(server.IsSubscribed(service, 0x0001U, {{127U, 0U, 0U, 2U}, 30510U}));
  REQUIRE(client == capture.Last().to);
  const auto ack = MessageView::Parse(osp::make_span(capture.Last().bytes,
                                                     capture.Last().size));
  REQUIRE(ack.has_value());
  auto ack_sd = ack.value().SdHeader();
  REQUIRE(ack_sd.has_value());
  EntryView ack_entry;
  REQUIRE(ack_sd.value().NextEntry(ack_entry));
  REQUIRE(Entry::Kind::kSubscribeAck == ack_entry.Kind());
  REQUIRE(10U == ack_entry.Ttl());

  const std::uint8_t payload[3] = {0xAAU, 0xBBU, 0xCCU};
  server.Publish(service, 0x0001U, 0x8001U, osp::make_span(payload), 200U);
  const Ipv4Endpoint event_endpoint{{127U, 0U, 0U, 2U}, 30510U};
  REQUIRE(event_endpoint == capture.Last().to);

  const auto notification = MessageView::Parse(osp::make_span(capture.Last().bytes,
                                                              capture.Last().size));
  REQUIRE(notification.has_value());
  REQUIRE(0x8001U == notification.value().Head().Id().MethodId());
  REQUIRE(0x1234U == notification.value().Head().Id().ServiceId());
  REQUIRE(osp::someip::MessageType::kNotification == notification.value().Head().Type().Type());
  REQUIRE(3U == notification.value().Body().size());
  REQUIRE(0xAAU == notification.value().Body()[0]);
  REQUIRE(0xCCU == notification.value().Body()[2]);
}

TEST_CASE("server - subscriptions expire and stop-subscribe removes them", "[server]") {
  Capture capture;
  Server<Capture, 4U, 4U> server(capture);
  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  REQUIRE(server.Offer(service, {{127U, 0U, 0U, 1U}, 30509U}, 60U, 0U));

  std::uint8_t subscribe[128] = {};
  const Ipv4Endpoint client{{127U, 0U, 0U, 2U}, 30490U};
  std::uint32_t size = BuildSubscribe(subscribe, 0x1234U, 0x0001U, 5U,
                                      {127U, 0U, 0U, 2U}, 30510U);
  server.HandleDatagram(osp::make_span(subscribe, size), client, 1000U);
  REQUIRE(1U == server.SubscriberCount());

  server.Tick(6001U);  // 5 s after the subscribe at 1000 ms
  REQUIRE(0U == server.SubscriberCount());

  size = BuildSubscribe(subscribe, 0x1234U, 0x0001U, 5U, {127U, 0U, 0U, 2U}, 30510U);
  server.HandleDatagram(osp::make_span(subscribe, size), client, 7000U);
  REQUIRE(1U == server.SubscriberCount());

  size = BuildSubscribe(subscribe, 0x1234U, 0x0001U, 0U, {127U, 0U, 0U, 2U}, 30510U);
  const std::uint32_t before = capture.count;
  server.HandleDatagram(osp::make_span(subscribe, size), client, 7100U);
  REQUIRE(0U == server.SubscriberCount());
  REQUIRE(before == capture.count);  // a stop-subscribe is not acknowledged
}

TEST_CASE("server - subscribe to an unoffered service is nacked", "[server]") {
  Capture capture;
  Server<Capture, 2U, 2U> server(capture);
  std::uint8_t subscribe[128] = {};
  const Ipv4Endpoint client{{127U, 0U, 0U, 2U}, 30490U};
  const std::uint32_t size = BuildSubscribe(subscribe, 0x9999U, 0x0001U, 5U,
                                            {127U, 0U, 0U, 2U}, 30510U);
  server.HandleDatagram(osp::make_span(subscribe, size), client, 0U);
  REQUIRE(1U == capture.count);
  REQUIRE(0U == server.SubscriberCount());

  const auto message = MessageView::Parse(osp::make_span(capture.Last().bytes,
                                                         capture.Last().size));
  REQUIRE(message.has_value());
  auto sd = message.value().SdHeader();
  REQUIRE(sd.has_value());
  EntryView entry;
  REQUIRE(sd.value().NextEntry(entry));
  REQUIRE(Entry::Kind::kSubscribeNack == entry.Kind());
  REQUIRE(0U == entry.Ttl());
}

TEST_CASE("server - subscriber slots are bounded", "[server]") {
  Capture capture;
  Server<Capture, 2U, 1U> server(capture);
  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  REQUIRE(server.Offer(service, {{127U, 0U, 0U, 1U}, 30509U}, 30U, 0U));

  std::uint8_t subscribe[128] = {};
  const Ipv4Endpoint client{{127U, 0U, 0U, 2U}, 30490U};
  std::uint32_t size = BuildSubscribe(subscribe, 0x1234U, 0x0001U, 5U,
                                      {127U, 0U, 0U, 2U}, 30510U);
  server.HandleDatagram(osp::make_span(subscribe, size), client, 0U);
  REQUIRE(1U == server.SubscriberCount());

  size = BuildSubscribe(subscribe, 0x1234U, 0x0002U, 5U, {127U, 0U, 0U, 3U}, 30511U);
  server.HandleDatagram(osp::make_span(subscribe, size), {{127U, 0U, 0U, 3U}, 30490U}, 0U);
  REQUIRE(1U == server.SubscriberCount());

  const auto message = MessageView::Parse(osp::make_span(capture.Last().bytes,
                                                         capture.Last().size));
  REQUIRE(message.has_value());
  auto sd = message.value().SdHeader();
  REQUIRE(sd.has_value());
  EntryView entry;
  REQUIRE(sd.value().NextEntry(entry));
  REQUIRE(Entry::Kind::kSubscribeNack == entry.Kind());
}

TEST_CASE("server - offers re-announce at half their ttl", "[server]") {
  Capture capture;
  Server<Capture, 2U, 2U> server(capture);
  const ServiceInstance service = ServiceInstance::New(0x2222U, 0x0003U, 2U);
  REQUIRE(server.Offer(service, {{10U, 0U, 0U, 1U}, 30600U}, 4U, 0U));
  REQUIRE(1U == capture.count);

  server.Tick(1999U);
  REQUIRE(1U == capture.count);
  server.Tick(2000U);
  REQUIRE(2U == capture.count);
  const Ipv4Endpoint multicast{{239U, 255U, 0U, 255U}, 30490U};
  REQUIRE(multicast == capture.Last().to);
  server.Tick(2500U);
  REQUIRE(2U == capture.count);
}

namespace {

struct EchoHandler final {
  std::uint32_t calls{0U};
  std::uint16_t last_method{0U};

  RequestReply OnRequest(const IncomingRequest& request) noexcept {
    ++calls;
    last_method = request.method_id;
    RequestReply reply;
    reply.handled = true;
    if (0xBEEFU == request.method_id) {
      reply.is_error = true;
      reply.code = ReturnCode::UnknownMethod();
    } else {
      reply.payload = request.payload;
    }
    return reply;
  }
};

std::uint32_t BuildRequest(std::uint8_t* out, std::uint16_t method_id,
                           std::uint32_t request_id, MessageType type,
                           osp::span<const std::uint8_t> payload) noexcept {
  const Header header = Header::New(
      MessageId::FromServiceAndMethod(0x1234U, method_id), request_id, 0x01U, 0x01U,
      MessageTypeField::New(type, false), ReturnCode::Ok(),
      static_cast<std::uint32_t>(payload.size()));
  const Message<OpaquePayload> message(header, OpaquePayload(payload));
  const auto written = message.EncodeTo(osp::make_span(out, 128U));
  return written.has_value() ? written.value() : 0U;
}

}  // namespace

TEST_CASE("server - requests are dispatched and answered", "[server][rpc]") {
  Capture capture;
  Server<Capture, 2U, 2U, 1400U, EchoHandler> server(capture);
  const Ipv4Endpoint client{{127U, 0U, 0U, 2U}, 30510U};
  const std::uint8_t payload[3] = {0x01U, 0x02U, 0x03U};

  std::uint8_t request_bytes[128] = {};
  const std::uint32_t request_size =
      BuildRequest(request_bytes, 0x0001U, 0x00010007U, MessageType::kRequest,
                   osp::make_span(payload));
  REQUIRE(0U != request_size);
  server.HandleDatagram(osp::make_span(request_bytes, request_size), client, 0U);

  REQUIRE(1U == server.Handler().calls);
  REQUIRE(0x0001U == server.Handler().last_method);
  REQUIRE(client == capture.Last().to);

  const auto response = MessageView::Parse(osp::make_span(capture.Last().bytes,
                                                          capture.Last().size));
  REQUIRE(response.has_value());
  REQUIRE(MessageType::kResponse == response.value().Head().Type().Type());
  REQUIRE(0x00010007U == response.value().Head().RequestId());
  REQUIRE(3U == response.value().Body().size());
  REQUIRE(0x03U == response.value().Body()[2]);
  REQUIRE(ReturnCode::Ok() == response.value().Head().Code());

  const std::uint32_t error_size =
      BuildRequest(request_bytes, 0xBEEFU, 0x00010008U, MessageType::kRequest,
                   osp::make_span(payload));
  server.HandleDatagram(osp::make_span(request_bytes, error_size), client, 0U);
  const auto error = MessageView::Parse(osp::make_span(capture.Last().bytes,
                                                       capture.Last().size));
  REQUIRE(error.has_value());
  REQUIRE(MessageType::kError == error.value().Head().Type().Type());
  REQUIRE(ReturnCode::UnknownMethod() == error.value().Head().Code());

  const std::uint32_t before = capture.count;
  const std::uint32_t fire_size =
      BuildRequest(request_bytes, 0x0002U, 0x00010009U, MessageType::kRequestNoReturn,
                   osp::make_span(payload));
  server.HandleDatagram(osp::make_span(request_bytes, fire_size), client, 0U);
  REQUIRE(before == capture.count);
  REQUIRE(3U == server.Handler().calls);
}

namespace {

struct CountingObserver final {
  std::uint32_t available{0U};
  std::uint32_t gone{0U};

  void OnServiceAvailable(ServiceInstance /*service*/) noexcept { ++available; }
  void OnServiceGone(ServiceInstance /*service*/) noexcept { ++gone; }
};

std::uint32_t EncodeOffer(std::uint8_t* out, const OfferServiceRequest& request,
                          bool stop) noexcept {
  auto session = SdSessionCounter::New();
  const auto built =
      stop ? BuildStopOfferServiceDatagram(osp::make_span(out, 128U), request, session.Next())
           : BuildOfferServiceDatagram(osp::make_span(out, 128U), request, session.Next());
  return built.has_value() ? built.value() : 0U;
}

OfferServiceRequest RemoteOffer(std::uint16_t service_id, std::uint32_t ttl) noexcept {
  OfferServiceRequest request;
  request.service_id = service_id;
  request.instance_id = 0x0001U;
  request.major_version = 1U;
  request.ttl = ttl;
  request.local_ip = {10U, 0U, 0U, 1U};
  request.unicast_port = 30600U;
  return request;
}

}  // namespace

TEST_CASE("server - remote offers are observed and expire", "[server][observer]") {
  Capture capture;
  Server<Capture, 2U, 2U, 1400U, osp::someip::NoRequestHandler, osp::someip::NoE2E,
         CountingObserver>
      server(capture);
  const Ipv4Endpoint remote{{127U, 0U, 0U, 2U}, 30490U};
  const OfferServiceRequest request = RemoteOffer(0x2222U, 2U);

  std::uint8_t buffer[128] = {};
  const std::uint32_t size = EncodeOffer(buffer, request, false);
  REQUIRE(0U != size);
  server.HandleDatagram(osp::make_span(buffer, size), remote, 0U);
  REQUIRE(1U == server.ObserverHandler().available);

  server.HandleDatagram(osp::make_span(buffer, size), remote, 100U);
  REQUIRE(1U == server.ObserverHandler().available);
  REQUIRE(0U == server.ObserverHandler().gone);

  server.Tick(2101U);  // 2 s after the refresh at 100 ms
  REQUIRE(1U == server.ObserverHandler().gone);
}

TEST_CASE("server - own announcement is not reported as remote", "[server][observer]") {
  Capture capture;
  Server<Capture, 2U, 2U, 1400U, osp::someip::NoRequestHandler, osp::someip::NoE2E,
         CountingObserver>
      server(capture);
  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  REQUIRE(server.Offer(service, {{127U, 0U, 0U, 1U}, 30509U}, 30U, 0U));

  std::uint8_t buffer[128] = {};
  const std::uint32_t size = EncodeOffer(buffer, RemoteOffer(0x1234U, 30U), false);
  REQUIRE(0U != size);
  server.HandleDatagram(osp::make_span(buffer, size), {{127U, 0U, 0U, 2U}, 30490U}, 0U);
  REQUIRE(0U == server.ObserverHandler().available);
}

TEST_CASE("server - wildcard find matches every offer", "[server][sd]") {
  Capture capture;
  Server<Capture, 2U, 2U> server(capture);
  REQUIRE(server.Offer(ServiceInstance::New(0x1111U, 0x0001U, 1U),
                       {{127U, 0U, 0U, 1U}, 30509U}, 30U, 0U));
  REQUIRE(server.Offer(ServiceInstance::New(0x2222U, 0x0001U, 1U),
                       {{127U, 0U, 0U, 1U}, 30510U}, 30U, 0U));
  const std::uint32_t after_announcements = capture.count;

  std::uint8_t find[64] = {};
  const auto find_size =
      BuildFindServiceDatagram(osp::make_span(find), 0xFFFFU, SdSessionCounter::New().Next());
  REQUIRE(find_size.has_value());
  server.HandleDatagram(osp::make_span(find, find_size.value()),
                        {{127U, 0U, 0U, 2U}, 30490U}, 10U);

  REQUIRE((after_announcements + 2U) == capture.count);
  std::uint32_t matched = 0U;
  for (std::uint32_t index = after_announcements; index < capture.count; ++index) {
    const auto reply = MessageView::Parse(
        osp::make_span(capture.items[index].bytes, capture.items[index].size));
    REQUIRE(reply.has_value());
    auto sd = reply.value().SdHeader();
    REQUIRE(sd.has_value());
    EntryView entry;
    REQUIRE(sd.value().NextEntry(entry));
    if (0x1111U == entry.ServiceId() || 0x2222U == entry.ServiceId()) {
      ++matched;
    }
  }
  REQUIRE(2U == matched);
}

TEST_CASE("server - offer slots are bounded and reused per service", "[server]") {
  Capture capture;
  Server<Capture, 2U, 4U, 256U> server(capture);
  const Ipv4Endpoint endpoint{{127U, 0U, 0U, 1U}, 30509U};
  const ServiceInstance first = ServiceInstance::New(0x1001U, 1U, 1U);
  const ServiceInstance second = ServiceInstance::New(0x1002U, 1U, 1U);
  const ServiceInstance third = ServiceInstance::New(0x1003U, 1U, 1U);

  REQUIRE(server.Offer(first, endpoint, 30U, 0U));
  REQUIRE(server.Offer(second, endpoint, 30U, 0U));
  REQUIRE(!server.Offer(third, endpoint, 30U, 0U));
  REQUIRE(2U == server.OfferedCount());

  // Offering the same service again updates its slot instead of taking a new one.
  REQUIRE(server.Offer(first, endpoint, 60U, 0U));
  REQUIRE(2U == server.OfferedCount());

  server.StopOffer(first);
  REQUIRE(1U == server.OfferedCount());
  REQUIRE(server.Offer(third, endpoint, 30U, 0U));
  REQUIRE(2U == server.OfferedCount());
}

TEST_CASE("server - publish without subscribers or oversized payload sends nothing", "[server]") {
  Capture capture;
  Server<Capture, 2U, 2U, 64U> server(capture);
  const Ipv4Endpoint endpoint{{127U, 0U, 0U, 1U}, 30509U};
  const ServiceInstance service = ServiceInstance::New(0x1234U, 1U, 1U);
  REQUIRE(server.Offer(service, endpoint, 30U, 0U));
  const std::uint32_t after_offer = capture.count;

  const std::uint8_t payload[4] = {1U, 2U, 3U, 4U};
  server.Publish(service, 0x0001U, 0x8001U, osp::make_span(payload), 100U);
  REQUIRE(after_offer == capture.count);

  const Ipv4Endpoint subscriber{{192U, 0U, 2U, 1U}, 30510U};
  std::uint8_t subscribe[128] = {};
  server.HandleDatagram(
      osp::make_span(subscribe, BuildSubscribe(subscribe, 0x1234U, 0x0001U, 10U,
                                               subscriber.address, subscriber.port)),
      subscriber, 100U);
  REQUIRE(1U == server.SubscriberCount());
  const std::uint32_t after_subscribe = capture.count;

  server.Publish(service, 0x0001U, 0x8001U, osp::make_span(payload), 200U);
  REQUIRE(after_subscribe + 1U == capture.count);

  // MaxDatagram is 64, so a payload above 48 bytes cannot be encoded.
  std::uint8_t oversized[49] = {};
  server.Publish(service, 0x0001U, 0x8001U, osp::make_span(oversized), 300U);
  REQUIRE(after_subscribe + 1U == capture.count);
}
