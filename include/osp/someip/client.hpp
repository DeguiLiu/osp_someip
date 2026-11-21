/**
 * @file client.hpp
 * @brief Transport-agnostic SOME/IP client core: discovery cache, find
 *        repetition, request/response matching, timeouts and event delivery.
 *
 * The client owns no socket and no clock. A compile-time sink sends batches
 * out; a compile-time handler receives responses, events, timeouts and
 * subscription acknowledgements. State lives in fixed-size arrays.
 */

#ifndef OSP_SOMEIP_CLIENT_HPP_
#define OSP_SOMEIP_CLIENT_HPP_

#include "osp/someip/endpoint.hpp"
#include "osp/someip/e2e.hpp"
#include "osp/someip/header.hpp"
#include "osp/someip/message.hpp"
#include "osp/someip/payload.hpp"
#include "osp/someip/sd/entry_view.hpp"
#include "osp/someip/sd/header.hpp"
#include "osp/someip/sd/session.hpp"
#include "osp/someip/sd_codec.hpp"
#include "osp/someip/service_instance.hpp"
#include "osp/someip/tp.hpp"
#include "osp/span.hpp"

#include <cstddef>
#include <cstdint>

namespace osp::someip {

/**
 * @brief SOME/IP client over caller-supplied transport and clock.
 *
 * The handler must provide:
 * @code
 *   void OnResponse(std::uint32_t request_id, MessageView message) noexcept;
 *   void OnEvent(MessageView message) noexcept;
 *   void OnTimeout(std::uint32_t request_id) noexcept;
 *   void OnServiceAvailable(ServiceInstance service) noexcept;
 *   void OnServiceGone(ServiceInstance service) noexcept;
 *   void OnSenderRebooted(const Ipv4Endpoint& source) noexcept;
 *   void OnSubscribeAck(std::uint16_t service_id, std::uint16_t instance_id,
 *                       std::uint16_t event_group_id, std::uint32_t granted_ttl_s) noexcept;
 * @endcode
 */
template <typename Sink, typename Handler, std::uint32_t MaxServices = 16U,
          std::uint32_t MaxPending = 4U, std::uint32_t MaxDatagram = 1400U,
          typename E2E = NoE2E, std::uint32_t MaxSubscriptions = 8U,
          std::uint32_t MaxPeers = 8U, typename Tp = NoTpReassembly>
class Client final {
 public:
  static constexpr std::uint32_t kFindRepetitions = 3U;
  static constexpr std::uint32_t kFindBaseDelayMs = 100U;
  /// @brief Session IDs start here after a reboot.
  static constexpr std::uint16_t kFirstSession = 1U;
  /// @brief Silence that separates a reboot from a reordered duplicate.
  static constexpr std::uint32_t kRebootQuietMs = 2000U;

  /// @brief A service learned from an OfferService entry.
  struct RemoteService final {
    ServiceInstance service{};
    Ipv4Endpoint endpoint{};
    Ipv4Endpoint sd_source{};
    std::uint32_t minor_version{0U};
    std::uint32_t expires_ms{0U};
    bool active{false};
  };

  Client(Sink& sink, Handler& handler, Ipv4Endpoint local_endpoint,
         Ipv4Endpoint sd_destination = SdMulticastEndpoint()) noexcept
      : sink_(sink),
        handler_(handler),
        local_endpoint_(local_endpoint),
        sd_destination_(sd_destination) {}

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  /// @brief Mutable access to the E2E policy (tests inspect its counters).
  E2E& E2EHandler() noexcept { return e2e_; }

  /// @brief Mutable access to the TP reassembler.
  Tp& TpHandler() noexcept { return tp_; }

  /// @brief Send a FindService now and repeat it at doubling intervals.
  bool StartFind(std::uint16_t service_id, std::uint32_t now_ms) noexcept {
    FindSlot* slot = FreeFindSlot(service_id);
    if (nullptr == slot) {
      return false;
    }
    slot->service_id = service_id;
    slot->remaining = kFindRepetitions;
    slot->interval_ms = kFindBaseDelayMs;
    slot->next_ms = now_ms;
    slot->active = true;
    SendFind(service_id);
    --slot->remaining;
    slot->next_ms = now_ms + slot->interval_ms;
    slot->interval_ms *= 2U;
    return true;
  }

  /// @brief Repeat due finds and report timed-out requests.
  void Tick(std::uint32_t now_ms) noexcept {
    tp_.Tick(now_ms);
    for (auto& slot : services_) {
      if (slot.active && now_ms >= slot.expires_ms) {
        slot.active = false;
        handler_.OnServiceGone(slot.service);
      }
    }
    for (auto& slot : finds_) {
      if (!slot.active || now_ms < slot.next_ms) {
        continue;
      }
      if (0U == slot.remaining) {
        slot.active = false;
        continue;
      }
      SendFind(slot.service_id);
      --slot.remaining;
      slot.next_ms = now_ms + slot.interval_ms;
      slot.interval_ms *= 2U;
    }
    for (auto& slot : pending_) {
      if (slot.active && now_ms >= slot.deadline_ms) {
        slot.active = false;
        handler_.OnTimeout(slot.request_id);
      }
    }
    for (auto& slot : subscriptions_) {
      if (!slot.active) {
        continue;
      }
      if (now_ms >= slot.expires_ms) {
        slot.active = false;
      } else if (now_ms >= slot.refresh_at_ms) {
        SendSubscribe(slot);
        slot.refresh_at_ms = now_ms + (slot.ttl_s * 1000U) / 2U;
      }
    }
  }

  /// @brief Handle one received datagram: SD, response, error or notification.
  void HandleDatagram(osp::span<const std::uint8_t> datagram, const Ipv4Endpoint& source,
                      std::uint32_t now_ms) noexcept {
    const auto message = MessageView::Parse(datagram);
    if (!message.has_value()) {
      return;
    }
    MessageView full = message.value();
    if (full.Head().Type().IsTp()) {
      MessageView done;
      if (!tp_.Feed(full, now_ms, done)) {
        return;
      }
      full = done;
    }
    if (full.IsSd()) {
      HandleSd(full, source, now_ms);
      return;
    }
    const HeaderView head = full.Head();
    const E2ECheck check = e2e_.Check(head.Id(), head.UpperHeaderBytes(), full.Body());
    if (check.applies && !check.ok) {
      return;
    }
    const MessageView body_view = full.WithBody(check.payload);
    const MessageType type = head.Type().Type();
    if (MessageType::kNotification == type) {
      handler_.OnEvent(body_view);
    } else if (MessageType::kResponse == type || MessageType::kError == type) {
      HandleResponse(body_view);
    }
  }

  /// @brief Send a request to a cached service; false if unknown or slots full.
  bool Request(const ServiceInstance& service, std::uint16_t method_id,
               osp::span<const std::uint8_t> payload, std::uint32_t timeout_ms,
               std::uint32_t now_ms) noexcept {
    const RemoteService* remote = FindCached(service);
    PendingSlot* slot = FreePendingSlot();
    if (nullptr == remote || nullptr == slot) {
      return false;
    }
    const std::uint32_t request_id = NextRequestId();
    const MessageId message_id =
        MessageId::FromServiceAndMethod(service.ServiceId(), method_id);
    const std::size_t capacity =
        sizeof(buffer_) - static_cast<std::size_t>(kHeaderSize);
    if (payload.size() > capacity) {
      return false;
    }
    for (std::size_t index = 0U; index < payload.size(); ++index) {
      buffer_[static_cast<std::size_t>(kHeaderSize) + index] = payload[index];
    }
    const Header provisional = Header::New(
        message_id, request_id, 0x01U, service.MajorVersion(),
        MessageTypeField::New(MessageType::kRequest, false), ReturnCode::Ok(),
        static_cast<std::uint32_t>(payload.size()));
    const auto protected_body = e2e_.Protect(
        message_id, provisional.UpperHeaderBytes(),
        osp::make_span(buffer_ + static_cast<std::size_t>(kHeaderSize), capacity),
        static_cast<std::uint32_t>(payload.size()));
    if (protected_body.data() != buffer_ + static_cast<std::size_t>(kHeaderSize)) {
      return false;
    }
    const Header header = Header::New(
        message_id, request_id, 0x01U, service.MajorVersion(),
        MessageTypeField::New(MessageType::kRequest, false), ReturnCode::Ok(),
        static_cast<std::uint32_t>(protected_body.size()));
    const auto written = header.EncodeTo(osp::span<std::uint8_t>(buffer_, kHeaderSize));
    if (!written.has_value()) {
      return false;
    }
    sink_(osp::span<const std::uint8_t>(
              buffer_, static_cast<std::size_t>(kHeaderSize) + protected_body.size()),
          remote->endpoint);
    slot->request_id = request_id;
    slot->deadline_ms = now_ms + timeout_ms;
    slot->active = true;
    return true;
  }

  /// @brief Send a SubscribeEventGroup for a cached service.
  bool Subscribe(const ServiceInstance& service, std::uint16_t event_group_id,
                 std::uint32_t ttl_s, std::uint32_t now_ms) noexcept {
    const RemoteService* remote = FindCached(service);
    SubscriptionSlot* slot = SubscriptionSlotFor(service, event_group_id);
    if (nullptr == remote || nullptr == slot || ttl_s > sd::kMaxTtl || 0U == ttl_s) {
      return false;
    }
    slot->service = service;
    slot->sd_source = remote->sd_source;
    slot->event_group_id = event_group_id;
    slot->ttl_s = ttl_s;
    slot->expires_ms = now_ms + (ttl_s * 1000U);
    slot->refresh_at_ms = now_ms + (ttl_s * 1000U) / 2U;
    slot->active = true;
    if (!SendSubscribe(*slot)) {
      slot->active = false;
      return false;
    }
    return true;
  }

  /// @brief Number of live subscriptions (acked or awaiting ack).
  std::uint32_t SubscriptionCount() const noexcept { return CountActive(subscriptions_); }

  bool IsSubscribed(const ServiceInstance& service,
                    std::uint16_t event_group_id) const noexcept {
    for (const auto& slot : subscriptions_) {
      if (slot.active && slot.service == service && slot.event_group_id == event_group_id) {
        return true;
      }
    }
    return false;
  }

  /// @brief Cached service matching service, instance and major version.
  const RemoteService* FindCached(const ServiceInstance& service) const noexcept {
    for (const auto& slot : services_) {
      if (slot.active && slot.service == service) {
        return &slot;
      }
    }
    return nullptr;
  }

  const RemoteService* FindCachedByService(std::uint16_t service_id) const noexcept {
    for (const auto& slot : services_) {
      if (slot.active && slot.service.ServiceId() == service_id) {
        return &slot;
      }
    }
    return nullptr;
  }

  std::uint32_t ServiceCount() const noexcept { return CountActive(services_); }
  std::uint32_t PendingCount() const noexcept { return CountActive(pending_); }

 private:
  struct FindSlot final {
    std::uint16_t service_id{0U};
    std::uint32_t remaining{0U};
    std::uint32_t interval_ms{0U};
    std::uint32_t next_ms{0U};
    bool active{false};
  };

  struct PendingSlot final {
    std::uint32_t request_id{0U};
    std::uint32_t deadline_ms{0U};
    bool active{false};
  };

  struct SubscriptionSlot final {
    ServiceInstance service{};
    Ipv4Endpoint sd_source{};
    std::uint16_t event_group_id{0U};
    std::uint32_t ttl_s{0U};
    std::uint32_t refresh_at_ms{0U};
    std::uint32_t expires_ms{0U};
    bool active{false};
  };

  struct PeerSlot final {
    Ipv4Endpoint source{};
    std::uint16_t session{0U};
    std::uint32_t last_ms{0U};
    bool active{false};
  };

  template <typename T, std::uint32_t N>
  static std::uint32_t CountActive(const T (&slots)[N]) noexcept {
    std::uint32_t count = 0U;
    for (const auto& slot : slots) {
      if (slot.active) {
        ++count;
      }
    }
    return count;
  }

  FindSlot* FreeFindSlot(std::uint16_t service_id) noexcept {
    FindSlot* free_slot = nullptr;
    for (auto& slot : finds_) {
      if (slot.active && slot.service_id == service_id) {
        return &slot;
      }
      if (!slot.active && nullptr == free_slot) {
        free_slot = &slot;
      }
    }
    return free_slot;
  }

  PendingSlot* FreePendingSlot() noexcept {
    for (auto& slot : pending_) {
      if (!slot.active) {
        return &slot;
      }
    }
    return nullptr;
  }

  SubscriptionSlot* SubscriptionSlotFor(const ServiceInstance& service,
                                        std::uint16_t event_group_id) noexcept {
    SubscriptionSlot* free_slot = nullptr;
    for (auto& slot : subscriptions_) {
      if (slot.active && slot.service == service && slot.event_group_id == event_group_id) {
        return &slot;
      }
      if (!slot.active && nullptr == free_slot) {
        free_slot = &slot;
      }
    }
    return free_slot;
  }

  /// @brief Records the sender's SD session; true when it restarted.
  bool TrackSession(const Ipv4Endpoint& source, std::uint16_t session,
                    sd::Flags flags, std::uint32_t now_ms) noexcept {
    PeerSlot* free_slot = nullptr;
    for (auto& slot : peers_) {
      if (slot.active && slot.source == source) {
        // The same offer can arrive twice, unicast and over multicast, and the
        // two copies may be reordered; a low session ID alone is not a reboot.
        // A peer that restarts while its reboot flag is still set starts again
        // at kFirstSession, which is the signal used here.
        // vsomeip answers a FindService with its own offer counter, so the
        // session ID drops back to 1 while the peer is perfectly healthy; only
        // a drop after real silence is taken as a reboot.
        const bool quiet = (now_ms - slot.last_ms) >= kRebootQuietMs;
        const bool rebooted =
            (sd::RebootFlag::kRecentlyRebooted == flags.Reboot()) &&
            (session <= kFirstSession) && (slot.session > kFirstSession) && quiet;
        slot.last_ms = now_ms;
        if (rebooted || session > slot.session) {
          slot.session = session;
        }
        return rebooted;
      }
      if (!slot.active && nullptr == free_slot) {
        free_slot = &slot;
      }
    }
    if (nullptr != free_slot) {
      free_slot->source = source;
      free_slot->session = session;
      free_slot->last_ms = now_ms;
      free_slot->active = true;
    }
    return false;
  }

  void HandleSenderReboot(const Ipv4Endpoint& source) noexcept {
    for (auto& slot : services_) {
      if (slot.active && slot.sd_source == source) {
        slot.active = false;
        handler_.OnServiceGone(slot.service);
      }
    }
    for (auto& slot : subscriptions_) {
      if (slot.active && slot.sd_source == source) {
        slot.active = false;
      }
    }
    handler_.OnSenderRebooted(source);
  }

  bool SendSubscribe(const SubscriptionSlot& slot) noexcept {
    SubscribeEventGroupRequest request;
    request.service_id = slot.service.ServiceId();
    request.instance_id = slot.service.InstanceId();
    request.major_version = slot.service.MajorVersion();
    request.event_group_id = slot.event_group_id;
    request.ttl = slot.ttl_s;
    request.local_ip = local_endpoint_.address;
    request.local_rx_port = local_endpoint_.port;
    const auto written = BuildSubscribeEventGroupDatagram(osp::make_span(buffer_), request,
                                                          session_.Next());
    if (!written.has_value()) {
      return false;
    }
    sink_(osp::span<const std::uint8_t>(buffer_, written.value()), slot.sd_source);
    return true;
  }

  void HandleSubscriptionResponse(const sd::EntryView& entry, std::uint32_t now_ms) noexcept {
    const ServiceInstance service =
        ServiceInstance::New(entry.ServiceId(), entry.InstanceId(), entry.MajorVersion());
    for (auto& slot : subscriptions_) {
      if (slot.active && slot.service == service &&
          slot.event_group_id == entry.EventGroupId()) {
        if (0U == entry.Ttl()) {
          slot.active = false;
        } else {
          slot.ttl_s = entry.Ttl();
          slot.expires_ms = now_ms + (slot.ttl_s * 1000U);
          slot.refresh_at_ms = now_ms + (slot.ttl_s * 1000U) / 2U;
        }
      }
    }
    handler_.OnSubscribeAck(entry.ServiceId(), entry.InstanceId(), entry.EventGroupId(),
                            entry.Ttl());
  }

  void HandleResponse(MessageView message) noexcept {
    const std::uint32_t request_id = message.Head().RequestId();
    for (auto& slot : pending_) {
      if (slot.active && slot.request_id == request_id) {
        slot.active = false;
        handler_.OnResponse(request_id, message);
      }
    }
  }

  void HandleSd(MessageView message, const Ipv4Endpoint& source,
                std::uint32_t now_ms) noexcept {
    auto sd_header = message.SdHeader();
    if (!sd_header.has_value()) {
      return;
    }
    const std::uint16_t session =
        static_cast<std::uint16_t>(message.Head().RequestId() & 0xFFFFU);
    if (TrackSession(source, session, sd_header.value().GetFlags(), now_ms)) {
      HandleSenderReboot(source);
    }
    sd::EntryView entry;
    while (sd_header.value().NextEntry(entry)) {
      switch (entry.Kind()) {
        case sd::Entry::Kind::kOfferService:
        case sd::Entry::Kind::kStopOfferService:
          HandleOffer(entry, sd_header.value(), source, now_ms);
          break;
        case sd::Entry::Kind::kSubscribeAck:
        case sd::Entry::Kind::kSubscribeNack:
          HandleSubscriptionResponse(entry, now_ms);
          break;
        default:
          break;
      }
    }
  }

  void HandleOffer(const sd::EntryView& entry, const sd::SdHeaderView& sd_header,
                   const Ipv4Endpoint& source, std::uint32_t now_ms) noexcept {
    const ServiceInstance service =
        ServiceInstance::New(entry.ServiceId(), entry.InstanceId(), entry.MajorVersion());
    RemoteService* slot = ServiceSlot(service);
    if (nullptr == slot) {
      return;
    }
    if (sd::Entry::Kind::kStopOfferService == entry.Kind() || 0U == entry.Ttl()) {
      const bool was_active = slot->active;
      slot->active = false;
      if (was_active) {
        handler_.OnServiceGone(service);
      }
      return;
    }
    const bool was_active = slot->active;
    sd::OptionView option;
    const bool has_udp = sd_header.UdpEndpointOption(entry, option);
    slot->service = service;
    slot->endpoint = has_udp ? Ipv4Endpoint{option.AddressV4(), option.Port()} : source;
    slot->sd_source = source;
    slot->minor_version = entry.MinorVersion();
    slot->expires_ms = now_ms + (entry.Ttl() * 1000U);
    slot->active = true;
    if (!was_active) {
      handler_.OnServiceAvailable(service);
    }
  }

  RemoteService* ServiceSlot(const ServiceInstance& service) noexcept {
    RemoteService* free_slot = nullptr;
    for (auto& slot : services_) {
      if (slot.active && slot.service == service) {
        return &slot;
      }
      if (!slot.active && nullptr == free_slot) {
        free_slot = &slot;
      }
    }
    return free_slot;
  }

  void SendFind(std::uint16_t service_id) noexcept {
    const auto written = BuildFindServiceDatagram(osp::make_span(buffer_), service_id,
                                                  session_.Next());
    if (written.has_value()) {
      sink_(osp::span<const std::uint8_t>(buffer_, written.value()), sd_destination_);
    }
  }

  std::uint32_t NextRequestId() noexcept {
    ++session_id_;
    if (0U == session_id_) {
      session_id_ = 1U;
    }
    return (kClientId << 16U) | session_id_;
  }

  static constexpr std::uint32_t kClientId = 1U;

  Sink& sink_;
  Handler& handler_;
  Ipv4Endpoint local_endpoint_{};
  Ipv4Endpoint sd_destination_{};
  FindSlot finds_[MaxServices]{};
  RemoteService services_[MaxServices]{};
  PendingSlot pending_[MaxPending]{};
  SubscriptionSlot subscriptions_[MaxSubscriptions]{};
  PeerSlot peers_[MaxPeers]{};
  E2E e2e_{};
  sd::SdSessionCounter session_ = sd::SdSessionCounter::New();
  std::uint16_t session_id_{0U};
  std::uint8_t buffer_[MaxDatagram]{};
  Tp tp_{};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_CLIENT_HPP_
