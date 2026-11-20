/**
 * @file server.hpp
 * @brief Transport-agnostic SOME/IP service side: offers, subscriptions,
 *        event publishing, and SD message handling.
 *
 * The server owns no socket and no clock. The caller injects a send sink and
 * passes the current time in milliseconds to every call, which keeps the
 * behaviour deterministic and testable. All state lives in fixed-size arrays
 * sized by the template parameters.
 */

#ifndef OSP_SOMEIP_SERVER_HPP_
#define OSP_SOMEIP_SERVER_HPP_

#include "osp/someip/endpoint.hpp"
#include "osp/someip/e2e.hpp"
#include "osp/someip/error.hpp"
#include "osp/someip/header.hpp"
#include "osp/someip/message.hpp"
#include "osp/someip/payload.hpp"
#include "osp/someip/sd/entry_view.hpp"
#include "osp/someip/sd/header.hpp"
#include "osp/someip/sd/session.hpp"
#include "osp/someip/sd_codec.hpp"
#include "osp/someip/service_instance.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <cstddef>
#include <cstdint>

namespace osp::someip {

/// @brief One inbound RPC request handed to the server's request handler.
struct IncomingRequest final {
  std::uint32_t request_id{0U};
  std::uint16_t service_id{0U};
  std::uint16_t method_id{0U};
  std::uint8_t interface_version{0U};
  /// @brief Payload view; valid only for the duration of OnRequest().
  osp::span<const std::uint8_t> payload{};
  Ipv4Endpoint from{};
};

/// @brief What the handler wants the server to send back.
struct RequestReply final {
  bool handled{false};
  bool is_error{false};
  ReturnCode code{ReturnCode::Ok()};
  /// @brief Payload view; the server encodes it before OnRequest() returns.
  osp::span<const std::uint8_t> payload{};
};

/// @brief Default handler: every request is ignored.
struct NoRequestHandler final {
  RequestReply OnRequest(const IncomingRequest& /*request*/) noexcept { return RequestReply{}; }
};

/// @brief Default observer for services offered by other nodes: reports nothing.
struct NoServiceObserver final {
  void OnServiceAvailable(ServiceInstance /*service*/) noexcept {}
  void OnServiceGone(ServiceInstance /*service*/) noexcept {}
};

/**
 * @brief SOME/IP service provider over caller-supplied transport and clock.
 *
 * @tparam Sink          Callable `(osp::span<const std::uint8_t>, const Ipv4Endpoint&)`
 *                       invoked for every outbound datagram. Resolved at
 *                       compile time: no type erasure, no heap, no vtable.
 * @tparam MaxOffers     Offered service instances.
 * @tparam MaxSubscribers Concurrent event-group subscriptions.
 * @tparam MaxDatagram   Size of the scratch buffer used to build one datagram.
 * @tparam RequestHandler Supplies `RequestReply OnRequest(const IncomingRequest&)`;
 *                       it is held by value and reachable through Handler().
 * @tparam E2E            End-to-end protection policy; NoE2E checks nothing.
 * @tparam Observer       Receives availability changes of remote offers.
 */
template <typename Sink, std::uint32_t MaxOffers = 8U, std::uint32_t MaxSubscribers = 16U,
          std::uint32_t MaxDatagram = 1400U, typename RequestHandler = NoRequestHandler,
          typename E2E = NoE2E, typename Observer = NoServiceObserver,
          std::uint32_t MaxRemoteServices = 8U>
class Server final {
 public:
  /// @brief @p sink must outlive the server and must not store the byte view.
  explicit Server(Sink& sink, Ipv4Endpoint sd_multicast = SdMulticastEndpoint()) noexcept
      : sink_(sink), sd_multicast_(sd_multicast) {}

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  /// @brief Mutable access to the request handler (tests inspect its counters).
  RequestHandler& Handler() noexcept { return handler_; }

  /// @brief Mutable access to the E2E policy (tests inspect its counters).
  E2E& E2EHandler() noexcept { return e2e_; }

  /// @brief Mutable access to the remote-service observer.
  Observer& ObserverHandler() noexcept { return observer_; }

  /// @brief Start offering a service and announce it once.
  bool Offer(const ServiceInstance& service, const Ipv4Endpoint& endpoint,
             std::uint32_t ttl_s, std::uint32_t now_ms) noexcept {
    OfferSlot* slot = FindOffer(service);
    if (nullptr == slot) {
      return false;
    }
    slot->service = service;
    slot->endpoint = endpoint;
    slot->ttl_s = ttl_s;
    slot->last_ms = now_ms;
    slot->active = true;
    SendOffer(*slot, sd_multicast_);
    return true;
  }

  /// @brief Stop offering a service and drop its subscriptions.
  void StopOffer(const ServiceInstance& service) noexcept {
    OfferSlot* slot = FindOffer(service);
    if (nullptr == slot || !slot->active) {
      return;
    }
    const OfferServiceRequest request = ToRequest(*slot, 0U);
    slot->active = false;
    Emit([&request](osp::span<std::uint8_t> out, sd::SdSession session) {
      return BuildStopOfferServiceDatagram(out, request, session);
    }, sd_multicast_);
    RemoveSubscribers(service);
  }

  /// @brief Expire subscriptions and re-announce offers at half their TTL.
  void Tick(std::uint32_t now_ms) noexcept {
    for (auto& slot : remotes_) {
      if (slot.active && now_ms >= slot.expires_ms) {
        slot.active = false;
        observer_.OnServiceGone(slot.service);
      }
    }
    for (auto& slot : subscribers_) {
      if (slot.active && now_ms >= slot.expires_ms) {
        slot.active = false;
      }
    }
    for (auto& slot : offers_) {
      if (!slot.active) {
        continue;
      }
      const std::uint32_t half_life_ms = (slot.ttl_s * 1000U) / 2U;
      if ((now_ms - slot.last_ms) >= half_life_ms) {
        slot.last_ms = now_ms;
        SendOffer(slot, sd_multicast_);
      }
    }
  }

  /// @brief Handle one received SD datagram from @p source.
  void HandleDatagram(osp::span<const std::uint8_t> datagram, const Ipv4Endpoint& source,
                      std::uint32_t now_ms) noexcept {
    const auto message = MessageView::Parse(datagram);
    if (!message.has_value()) {
      return;
    }
    if (!message.value().IsSd()) {
      const MessageType type = message.value().Head().Type().Type();
      if (MessageType::kRequest == type || MessageType::kRequestNoReturn == type) {
        HandleRequest(message.value(), source);
      }
      return;
    }
    auto sd_header = message.value().SdHeader();
    if (!sd_header.has_value()) {
      return;
    }
    HandleSd(sd_header.value(), source, now_ms);
  }

  /// @brief Send one notification to every subscriber of the event group.
  void Publish(const ServiceInstance& service, std::uint16_t event_group_id,
               std::uint16_t event_id, osp::span<const std::uint8_t> payload,
               std::uint32_t now_ms) noexcept {
    const MessageId message_id =
        MessageId::FromServiceAndMethod(service.ServiceId(), event_id);
    const std::uint32_t request_id = static_cast<std::uint32_t>(session_.Next().id);
    const std::size_t capacity =
        sizeof(buffer_) - static_cast<std::size_t>(kHeaderSize);
    if (payload.size() > capacity) {
      return;
    }
    for (std::size_t index = 0U; index < payload.size(); ++index) {
      buffer_[static_cast<std::size_t>(kHeaderSize) + index] = payload[index];
    }
    const Header provisional = Header::New(
        message_id, request_id, 0x01U, service.MajorVersion(),
        MessageTypeField::New(MessageType::kNotification, false), ReturnCode::Ok(),
        static_cast<std::uint32_t>(payload.size()));
    const auto protected_body = e2e_.Protect(
        message_id, provisional.UpperHeaderBytes(),
        osp::make_span(buffer_ + static_cast<std::size_t>(kHeaderSize), capacity),
        static_cast<std::uint32_t>(payload.size()));
    if (protected_body.data() != buffer_ + static_cast<std::size_t>(kHeaderSize)) {
      return;
    }
    const Header header = Header::New(
        message_id, request_id, 0x01U, service.MajorVersion(),
        MessageTypeField::New(MessageType::kNotification, false), ReturnCode::Ok(),
        static_cast<std::uint32_t>(protected_body.size()));
    const auto written = header.EncodeTo(osp::span<std::uint8_t>(buffer_, kHeaderSize));
    if (!written.has_value()) {
      return;
    }
    const auto datagram = osp::span<const std::uint8_t>(
        buffer_, static_cast<std::size_t>(kHeaderSize) + protected_body.size());
    for (auto& slot : subscribers_) {
      if (!slot.active || !(slot.service == service) ||
          slot.event_group_id != event_group_id || now_ms >= slot.expires_ms) {
        continue;
      }
      sink_(datagram, slot.endpoint);
    }
  }

  std::uint32_t OfferedCount() const noexcept { return CountActive(offers_); }
  std::uint32_t SubscriberCount() const noexcept { return CountActive(subscribers_); }

  bool IsSubscribed(const ServiceInstance& service, std::uint16_t event_group_id,
                    const Ipv4Endpoint& endpoint) const noexcept {
    for (const auto& slot : subscribers_) {
      if (slot.active && slot.service == service && slot.event_group_id == event_group_id &&
          slot.endpoint == endpoint) {
        return true;
      }
    }
    return false;
  }

 private:
  struct OfferSlot final {
    ServiceInstance service{};
    Ipv4Endpoint endpoint{};
    std::uint32_t ttl_s{0U};
    std::uint32_t last_ms{0U};
    bool active{false};
  };

  struct SubscriberSlot final {
    ServiceInstance service{};
    Ipv4Endpoint endpoint{};
    std::uint16_t event_group_id{0U};
    std::uint32_t expires_ms{0U};
    bool active{false};
  };

  struct RemoteSlot final {
    ServiceInstance service{};
    Ipv4Endpoint endpoint{};
    std::uint32_t expires_ms{0U};
    bool active{false};
  };

  template <std::uint32_t N>
  static std::uint32_t CountActive(const OfferSlot (&slots)[N]) noexcept {
    std::uint32_t count = 0U;
    for (const auto& slot : slots) {
      if (slot.active) {
        ++count;
      }
    }
    return count;
  }

  template <std::uint32_t N>
  static std::uint32_t CountActive(const SubscriberSlot (&slots)[N]) noexcept {
    std::uint32_t count = 0U;
    for (const auto& slot : slots) {
      if (slot.active) {
        ++count;
      }
    }
    return count;
  }

  OfferSlot* FindOffer(const ServiceInstance& service) noexcept {
    OfferSlot* free_slot = nullptr;
    for (auto& slot : offers_) {
      if (slot.active && slot.service == service) {
        return &slot;
      }
      if (!slot.active && nullptr == free_slot) {
        free_slot = &slot;
      }
    }
    return free_slot;
  }

  const OfferSlot* MatchOffer(const sd::EntryView& entry) const noexcept {
    for (const auto& slot : offers_) {
      if (slot.active && Matches(slot.service, entry)) {
        return &slot;
      }
    }
    return nullptr;
  }

  bool IsOwnOffer(const ServiceInstance& service) const noexcept {
    for (const auto& slot : offers_) {
      if (slot.active && slot.service == service) {
        return true;
      }
    }
    return false;
  }

  RemoteSlot* RemoteSlotFor(const ServiceInstance& service) noexcept {
    RemoteSlot* free_slot = nullptr;
    for (auto& slot : remotes_) {
      if (slot.active && slot.service == service) {
        return &slot;
      }
      if (!slot.active && nullptr == free_slot) {
        free_slot = &slot;
      }
    }
    return free_slot;
  }

  void HandleRemoteOffer(const sd::EntryView& entry, const sd::SdHeaderView& sd_header,
                         const Ipv4Endpoint& source, std::uint32_t now_ms) noexcept {
    const ServiceInstance service =
        ServiceInstance::New(entry.ServiceId(), entry.InstanceId(), entry.MajorVersion());
    if (IsOwnOffer(service)) {
      return;  // our own announcement looped back through multicast
    }
    RemoteSlot* slot = RemoteSlotFor(service);
    if (nullptr == slot) {
      return;
    }
    const bool was_active = slot->active;
    sd::OptionView option;
    const bool has_option = sd_header.OptionAt(entry.IndexFirstOptionsRun(), option);
    slot->service = service;
    slot->endpoint = (has_option && option.IsKnown() &&
                      sd::OptionType::kIpV4Endpoint == option.Type())
                         ? Ipv4Endpoint{option.AddressV4(), option.Port()}
                         : source;
    slot->expires_ms = now_ms + (entry.Ttl() * 1000U);
    slot->active = true;
    if (!was_active) {
      observer_.OnServiceAvailable(service);
    }
  }

  void HandleRemoteStop(const sd::EntryView& entry) noexcept {
    const ServiceInstance service =
        ServiceInstance::New(entry.ServiceId(), entry.InstanceId(), entry.MajorVersion());
    for (auto& slot : remotes_) {
      if (slot.active && slot.service == service) {
        slot.active = false;
        observer_.OnServiceGone(service);
      }
    }
  }

  void HandleRequest(MessageView message, const Ipv4Endpoint& source) noexcept {
    const HeaderView head = message.Head();
    const E2ECheck check = e2e_.Check(head.Id(), head.UpperHeaderBytes(), message.Body());
    if (check.applies && !check.ok) {
      return;
    }
    IncomingRequest request;
    request.request_id = head.RequestId();
    request.service_id = head.Id().ServiceId();
    request.method_id = head.Id().MethodId();
    request.interface_version = head.InterfaceVersion();
    request.payload = check.payload;
    request.from = source;
    const RequestReply reply = handler_.OnRequest(request);
    if (!reply.handled || MessageType::kRequestNoReturn == head.Type().Type()) {
      return;
    }
    SendReply(head, reply, source);
  }

  void SendReply(const HeaderView& request, const RequestReply& reply,
                 const Ipv4Endpoint& to) noexcept {
    const Header header = Header::New(
        request.Id(), request.RequestId(), request.ProtocolVersion(),
        request.InterfaceVersion(),
        MessageTypeField::New(
            reply.is_error ? MessageType::kError : MessageType::kResponse, false),
        reply.code, static_cast<std::uint32_t>(reply.payload.size()));
    const Message<OpaquePayload> message(header, OpaquePayload(reply.payload));
    const auto written = message.EncodeTo(osp::make_span(buffer_));
    if (written.has_value()) {
      sink_(osp::span<const std::uint8_t>(buffer_, written.value()), to);
    }
  }

  void HandleSd(sd::SdHeaderView& sd_header, const Ipv4Endpoint& source,
                std::uint32_t now_ms) noexcept {
    sd::EntryView entry;
    while (sd_header.NextEntry(entry)) {
      switch (entry.Kind()) {
        case sd::Entry::Kind::kFindService:
          HandleFind(entry, source);
          break;
        case sd::Entry::Kind::kSubscribe:
          HandleSubscribe(entry, sd_header, source, now_ms);
          break;
        case sd::Entry::Kind::kOfferService:
          HandleRemoteOffer(entry, sd_header, source, now_ms);
          break;
        case sd::Entry::Kind::kStopOfferService:
          HandleRemoteStop(entry);
          break;
        default:
          break;
      }
    }
  }

  void HandleFind(const sd::EntryView& entry, const Ipv4Endpoint& source) noexcept {
    for (const auto& slot : offers_) {
      if (slot.active && Matches(slot.service, entry)) {
        SendOffer(slot, source);
      }
    }
  }

  void HandleSubscribe(const sd::EntryView& entry, const sd::SdHeaderView& sd_header,
                       const Ipv4Endpoint& source, std::uint32_t now_ms) noexcept {
    const OfferSlot* offer = MatchOffer(entry);
    const ServiceInstance service =
        ServiceInstance::New(entry.ServiceId(), entry.InstanceId(), entry.MajorVersion());
    if (0U == entry.Ttl()) {
      RemoveSubscriber(service, entry.EventGroupId(), source);
      return;
    }
    if (nullptr == offer) {
      SendAck(service, entry.EventGroupId(), 0U, source);
      return;
    }
    const Ipv4Endpoint endpoint = ResolveEndpoint(entry, sd_header, source);
    const std::uint32_t granted =
        (entry.Ttl() < offer->ttl_s) ? entry.Ttl() : offer->ttl_s;
    if (0U == granted || !AddSubscriber(service, endpoint, entry.EventGroupId(), granted, now_ms)) {
      SendAck(service, entry.EventGroupId(), 0U, source);
      return;
    }
    SendAck(service, entry.EventGroupId(), granted, source);
  }

  static Ipv4Endpoint ResolveEndpoint(const sd::EntryView& entry,
                                      const sd::SdHeaderView& sd_header,
                                      const Ipv4Endpoint& source) noexcept {
    sd::OptionView option;
    const bool found = sd_header.OptionAt(entry.IndexFirstOptionsRun(), option);
    if (found && option.IsKnown() && sd::OptionType::kIpV4Endpoint == option.Type()) {
      return Ipv4Endpoint{option.AddressV4(), option.Port()};
    }
    return source;
  }

  bool AddSubscriber(const ServiceInstance& service, const Ipv4Endpoint& endpoint,
                     std::uint16_t event_group_id, std::uint32_t ttl_s,
                     std::uint32_t now_ms) noexcept {
    SubscriberSlot* free_slot = nullptr;
    for (auto& slot : subscribers_) {
      if (slot.active && slot.service == service && slot.event_group_id == event_group_id &&
          slot.endpoint == endpoint) {
        slot.expires_ms = now_ms + (ttl_s * 1000U);
        return true;
      }
      if (!slot.active && nullptr == free_slot) {
        free_slot = &slot;
      }
    }
    if (nullptr == free_slot) {
      return false;
    }
    free_slot->service = service;
    free_slot->endpoint = endpoint;
    free_slot->event_group_id = event_group_id;
    free_slot->expires_ms = now_ms + (ttl_s * 1000U);
    free_slot->active = true;
    return true;
  }

  void RemoveSubscriber(const ServiceInstance& service, std::uint16_t event_group_id,
                        const Ipv4Endpoint& source) noexcept {
    for (auto& slot : subscribers_) {
      if (slot.active && slot.service == service && slot.event_group_id == event_group_id &&
          slot.endpoint.address == source.address) {
        slot.active = false;
      }
    }
  }

  void RemoveSubscribers(const ServiceInstance& service) noexcept {
    for (auto& slot : subscribers_) {
      if (slot.active && slot.service == service) {
        slot.active = false;
      }
    }
  }

  static bool Matches(const ServiceInstance& service, const sd::EntryView& entry) noexcept {
    const bool service_ok =
        0xFFFFU == entry.ServiceId() || entry.ServiceId() == service.ServiceId();
    const bool instance_ok =
        0xFFFFU == entry.InstanceId() || entry.InstanceId() == service.InstanceId();
    const bool major_ok =
        0xFFU == entry.MajorVersion() || entry.MajorVersion() == service.MajorVersion();
    return service_ok && instance_ok && major_ok;
  }

  static OfferServiceRequest ToRequest(const OfferSlot& slot, std::uint32_t ttl_s) noexcept {
    OfferServiceRequest request;
    request.service_id = slot.service.ServiceId();
    request.instance_id = slot.service.InstanceId();
    request.major_version = slot.service.MajorVersion();
    request.minor_version = 0U;
    request.ttl = ttl_s;
    request.local_ip = slot.endpoint.address;
    request.unicast_port = slot.endpoint.port;
    return request;
  }

  void SendOffer(const OfferSlot& slot, const Ipv4Endpoint& to) noexcept {
    const OfferServiceRequest request = ToRequest(slot, slot.ttl_s);
    Emit([&request](osp::span<std::uint8_t> out, sd::SdSession session) {
      return BuildOfferServiceDatagram(out, request, session);
    }, to);
  }

  void SendAck(const ServiceInstance& service, std::uint16_t event_group_id,
               std::uint32_t granted_ttl_s, const Ipv4Endpoint& to) noexcept {
    SubscribeAckRequest request;
    request.service_id = service.ServiceId();
    request.instance_id = service.InstanceId();
    request.event_group_id = event_group_id;
    request.major_version = service.MajorVersion();
    request.ttl = granted_ttl_s;
    Emit([&request](osp::span<std::uint8_t> out, sd::SdSession session) {
      return BuildSubscribeAckDatagram(out, request, session);
    }, to);
  }

  template <typename Builder>
  void Emit(Builder build, const Ipv4Endpoint& to) noexcept {
    const auto written = build(osp::make_span(buffer_), session_.Next());
    if (written.has_value()) {
      sink_(osp::span<const std::uint8_t>(buffer_, written.value()), to);
    }
  }

  Sink& sink_;
  Ipv4Endpoint sd_multicast_{};
  RequestHandler handler_{};
  E2E e2e_{};
  Observer observer_{};
  OfferSlot offers_[MaxOffers]{};
  SubscriberSlot subscribers_[MaxSubscribers]{};
  RemoteSlot remotes_[MaxRemoteServices]{};
  sd::SdSessionCounter session_ = sd::SdSessionCounter::New();
  std::uint8_t buffer_[MaxDatagram]{};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_SERVER_HPP_
