/**
 * @file test_e2e.cpp
 * @brief End-to-end policy hook: protect on send, check on receive, drop on failure.
 */

#include "osp/someip/client.hpp"
#include "osp/someip/server.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

using osp::someip::Client;
using osp::someip::E2ECheck;
using osp::someip::E2EStatus;
using osp::someip::Header;
using osp::someip::IncomingRequest;
using osp::someip::Ipv4Endpoint;
using osp::someip::Message;
using osp::someip::MessageId;
using osp::someip::MessageType;
using osp::someip::MessageTypeField;
using osp::someip::MessageView;
using osp::someip::OpaquePayload;
using osp::someip::RequestReply;
using osp::someip::ReturnCode;
using osp::someip::Server;
using osp::someip::ServiceInstance;

namespace {

constexpr std::uint8_t kTag = 0xA5U;

/// Appends a 1-byte tag on protect, verifies and strips it on check.
struct TagE2E final {
  std::uint32_t checks{0U};
  std::uint32_t protects{0U};
  bool reject_all{false};

  E2ECheck Check(MessageId /*id*/, const std::array<std::uint8_t, 8U>& /*upper*/,
                 osp::span<const std::uint8_t> payload) noexcept {
    ++checks;
    E2ECheck result;
    result.applies = true;
    result.payload = payload;
    const bool tag_ok =
        !payload.empty() && kTag == payload[payload.size() - 1U] && !reject_all;
    if (!tag_ok) {
      result.ok = false;
      result.status = E2EStatus::kError;
      return result;
    }
    result.ok = true;
    result.status = E2EStatus::kOk;
    result.payload = osp::span<const std::uint8_t>(payload.data(), payload.size() - 1U);
    return result;
  }

  osp::span<const std::uint8_t> Protect(MessageId /*id*/,
                                        const std::array<std::uint8_t, 8U>& /*upper*/,
                                        osp::span<std::uint8_t> buffer,
                                        std::uint32_t payload_size) noexcept {
    ++protects;
    if ((static_cast<std::size_t>(payload_size) + 1U) > buffer.size()) {
      return osp::span<const std::uint8_t>(buffer.data(), 0U);
    }
    buffer[payload_size] = kTag;
    return osp::span<const std::uint8_t>(buffer.data(),
                                         static_cast<std::size_t>(payload_size) + 1U);
  }
};

struct Capture final {
  std::uint8_t bytes[256]{};
  std::uint32_t size{0U};
  std::uint32_t count{0U};
  Ipv4Endpoint to{};

  void operator()(osp::span<const std::uint8_t> datagram, const Ipv4Endpoint& dest) noexcept {
    if (datagram.size() <= sizeof(bytes)) {
      std::memcpy(bytes, datagram.data(), datagram.size());
      size = static_cast<std::uint32_t>(datagram.size());
      to = dest;
      ++count;
    }
  }
};

struct EventHandler final {
  std::uint32_t events{0U};
  std::uint32_t size{0U};
  std::uint8_t first{0U};

  void OnResponse(std::uint32_t /*id*/, MessageView /*message*/) noexcept {}
  void OnEvent(MessageView message) noexcept {
    ++events;
    size = static_cast<std::uint32_t>(message.Body().size());
    first = 0U == size ? 0U : message.Body()[0];
  }
  void OnTimeout(std::uint32_t /*id*/) noexcept {}
  void OnSubscribeAck(std::uint16_t /*s*/, std::uint16_t /*i*/, std::uint16_t /*eg*/,
                      std::uint32_t /*ttl*/) noexcept {}
};

struct CountingRequestHandler final {
  std::uint32_t calls{0U};
  RequestReply OnRequest(const IncomingRequest& request) noexcept {
    ++calls;
    RequestReply reply;
    reply.handled = true;
    reply.payload = request.payload;
    return reply;
  }
};

std::uint32_t EncodeNotification(std::uint8_t* out, osp::span<const std::uint8_t> payload,
                                 bool with_tag) noexcept {
  const std::uint32_t body_size =
      static_cast<std::uint32_t>(payload.size()) + (with_tag ? 1U : 0U);
  const Header header = Header::NewEvent(0x1234U, 0x8001U, 9U, 0x01U, 1U, body_size);
  std::uint8_t body[256] = {};
  for (std::size_t i = 0U; i < payload.size(); ++i) {
    body[i] = payload[i];
  }
  if (with_tag) {
    body[payload.size()] = kTag;
  }
  const Message<OpaquePayload> tagged(header, OpaquePayload(osp::span<const std::uint8_t>(
                                                  body, body_size)));
  const auto written = tagged.EncodeTo(osp::make_span(out, 256U));
  return written.has_value() ? written.value() : 0U;
}

std::uint32_t EncodeRequest(std::uint8_t* out, osp::span<const std::uint8_t> payload,
                            bool with_tag) noexcept {
  const std::uint32_t body_size =
      static_cast<std::uint32_t>(payload.size()) + (with_tag ? 1U : 0U);
  const Header header = Header::New(
      MessageId::FromServiceAndMethod(0x1234U, 0x0001U), 0x00010007U, 0x01U, 0x01U,
      MessageTypeField::New(MessageType::kRequest, false), ReturnCode::Ok(), body_size);
  std::uint8_t body[256] = {};
  for (std::size_t i = 0U; i < payload.size(); ++i) {
    body[i] = payload[i];
  }
  if (with_tag) {
    body[payload.size()] = kTag;
  }
  const Message<OpaquePayload> message(
      header, OpaquePayload(osp::span<const std::uint8_t>(body, body_size)));
  const auto written = message.EncodeTo(osp::make_span(out, 256U));
  return written.has_value() ? written.value() : 0U;
}

}  // namespace

TEST_CASE("e2e - published payload carries the protection trailer", "[e2e][server]") {
  Capture capture;
  Server<Capture, 2U, 2U, 1400U, osp::someip::NoRequestHandler, TagE2E> server(capture);
  const ServiceInstance service = ServiceInstance::New(0x1234U, 0x0001U, 1U);
  REQUIRE(server.Offer(service, {{127U, 0U, 0U, 1U}, 30509U}, 30U, 0U));

  osp::someip::SubscribeEventGroupRequest subscribe;
  subscribe.service_id = 0x1234U;
  subscribe.instance_id = 0x0001U;
  subscribe.major_version = 1U;
  subscribe.event_group_id = 0x0001U;
  subscribe.ttl = 10U;
  subscribe.local_ip = {127U, 0U, 0U, 2U};
  subscribe.local_rx_port = 30510U;
  std::uint8_t subscribe_bytes[128] = {};
  const auto subscribe_size = osp::someip::BuildSubscribeEventGroupDatagram(
      osp::make_span(subscribe_bytes), subscribe,
      osp::someip::sd::SdSessionCounter::New().Next());
  REQUIRE(subscribe_size.has_value());
  server.HandleDatagram(
      osp::make_span(subscribe_bytes, subscribe_size.value()),
      {{127U, 0U, 0U, 2U}, 30490U}, 10U);

  const std::uint8_t payload[2] = {0x11U, 0x22U};
  server.Publish(service, 0x0001U, 0x8001U, osp::make_span(payload), 20U);
  REQUIRE(1U == server.E2EHandler().protects);
  REQUIRE(0U != capture.size);

  const auto message = MessageView::Parse(osp::make_span(capture.bytes, capture.size));
  REQUIRE(message.has_value());
  REQUIRE(3U == message.value().Body().size());
  REQUIRE(kTag == message.value().Body()[2]);
}

TEST_CASE("e2e - client strips the trailer and drops failing messages", "[e2e][client]") {
  Capture sink;
  EventHandler handler;
  Client<Capture, EventHandler, 4U, 4U, 1400U, TagE2E> client(
      sink, handler, {{127U, 0U, 0U, 1U}, 30510U});

  const std::uint8_t payload[2] = {0x33U, 0x44U};
  std::uint8_t bytes[256] = {};
  const std::uint32_t size = EncodeNotification(bytes, osp::make_span(payload), true);
  REQUIRE(0U != size);
  client.HandleDatagram(osp::make_span(bytes, size), {{127U, 0U, 0U, 2U}, 30501U}, 0U);
  REQUIRE(1U == handler.events);
  REQUIRE(2U == handler.size);
  REQUIRE(0x33U == handler.first);
  REQUIRE(1U == client.E2EHandler().checks);

  client.E2EHandler().reject_all = true;
  client.HandleDatagram(osp::make_span(bytes, size), {{127U, 0U, 0U, 2U}, 30501U}, 0U);
  REQUIRE(1U == handler.events);
  REQUIRE(2U == client.E2EHandler().checks);
}

TEST_CASE("e2e - server drops requests that fail the check", "[e2e][server][rpc]") {
  Capture capture;
  Server<Capture, 2U, 2U, 1400U, CountingRequestHandler, TagE2E> server(capture);

  const std::uint8_t payload[2] = {0x55U, 0x66U};
  std::uint8_t request[128] = {};
  const std::uint32_t without_tag = EncodeRequest(request, osp::make_span(payload), false);
  REQUIRE(0U != without_tag);
  server.HandleDatagram(osp::make_span(request, without_tag),
                        {{127U, 0U, 0U, 2U}, 30510U}, 0U);
  REQUIRE(0U == server.Handler().calls);

  const std::uint32_t with_tag = EncodeRequest(request, osp::make_span(payload), true);
  REQUIRE(0U != with_tag);
  server.HandleDatagram(osp::make_span(request, with_tag),
                        {{127U, 0U, 0U, 2U}, 30510U}, 0U);
  REQUIRE(1U == server.Handler().calls);
}
