/**
 * @file sd_codec.hpp
 * @brief Build complete SOME/IP-SD datagrams into a caller-owned buffer.
 *
 * Each builder writes the 16-byte SOME/IP wrapper and the SD payload in one
 * forward pass; the SOME/IP length field is known before the first byte is
 * written because the SD header size is computable up front. Nothing here
 * allocates, throws, or opens a socket.
 */

#ifndef OSP_SOMEIP_SD_CODEC_HPP_
#define OSP_SOMEIP_SD_CODEC_HPP_

#include "osp/someip/error.hpp"
#include "osp/someip/header.hpp"
#include "osp/someip/sd/entry.hpp"
#include "osp/someip/sd/header.hpp"
#include "osp/someip/sd/options.hpp"
#include "osp/someip/sd/session.hpp"
#include "osp/someip/sd/types.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <array>
#include <cstdint>

namespace osp::someip {

/// @brief One OfferService/StopOfferService entry plus its IPv4 endpoint.
struct OfferServiceRequest final {
  std::uint16_t service_id{0U};
  std::uint16_t instance_id{0U};
  std::uint8_t major_version{0U};
  std::uint32_t minor_version{0U};
  /// @brief Offer lifetime in seconds, at most sd::kMaxTtl.
  std::uint32_t ttl{0U};
  std::array<std::uint8_t, 4U> local_ip{};
  std::uint16_t unicast_port{0U};
};

/// @brief One SubscribeEventGroup entry plus the subscriber's IPv4 endpoint.
struct SubscribeEventGroupRequest final {
  std::uint16_t service_id{0U};
  std::uint16_t instance_id{0U};
  std::uint8_t major_version{0U};
  std::uint16_t event_group_id{0U};
  /// @brief Requested subscription lifetime in seconds.
  std::uint32_t ttl{0U};
  std::array<std::uint8_t, 4U> local_ip{};
  std::uint16_t local_rx_port{0U};
};

/// @brief One SubscribeEventGroupAck entry (no options).
struct SubscribeAckRequest final {
  std::uint16_t service_id{0U};
  std::uint16_t instance_id{0U};
  std::uint16_t event_group_id{0U};
  std::uint8_t major_version{0U};
  /// @brief Granted lifetime in seconds; 0 would make it a nack.
  std::uint32_t ttl{0U};
  std::uint16_t counter{0U};
};

namespace detail {

osp::expected<std::uint32_t, Error> WriteDatagram(
    osp::span<std::uint8_t> out, osp::span<const sd::Entry> entries,
    osp::span<const sd::Options> options, sd::SdSession session) noexcept;

osp::expected<std::uint32_t, Error> BuildServiceDatagram(
    osp::span<std::uint8_t> out, const OfferServiceRequest& request,
    sd::SdSession session, bool stop) noexcept;

}  // namespace detail

/// @brief Encode one OfferService datagram; returns its byte length.
inline osp::expected<std::uint32_t, Error> BuildOfferServiceDatagram(
    osp::span<std::uint8_t> out, const OfferServiceRequest& request,
    sd::SdSession session) noexcept {
  return detail::BuildServiceDatagram(out, request, session, false);
}

/// @brief Encode one StopOfferService datagram (TTL forced to 0).
inline osp::expected<std::uint32_t, Error> BuildStopOfferServiceDatagram(
    osp::span<std::uint8_t> out, const OfferServiceRequest& request,
    sd::SdSession session) noexcept {
  return detail::BuildServiceDatagram(out, request, session, true);
}

/// @brief Encode one SubscribeEventGroup datagram; returns its byte length.
inline osp::expected<std::uint32_t, Error> BuildSubscribeEventGroupDatagram(
    osp::span<std::uint8_t> out, const SubscribeEventGroupRequest& request,
    sd::SdSession session) noexcept {
  if (request.ttl > sd::kMaxTtl) {
    return osp::expected<std::uint32_t, Error>::error(Error::InvalidTtl(request.ttl));
  }
  sd::EventGroupEntry payload;
  payload.options_count = sd::OptionsCount(1U, 0U);
  payload.service_id = request.service_id;
  payload.instance_id = request.instance_id;
  payload.major_version = request.major_version;
  payload.ttl = request.ttl;
  payload.counter = 0U;
  payload.event_group_id = request.event_group_id;
  const sd::Entry entries[1] = {sd::Entry::Subscribe(payload)};
  const sd::Options options[1] = {
      sd::Options::IpV4Endpoint(request.local_ip, sd::TransportProtocol::kUdp,
                                request.local_rx_port)};
  return detail::WriteDatagram(out, osp::make_span(entries), osp::make_span(options), session);
}

/// @brief Encode one SubscribeEventGroupAck datagram; returns its byte length.
inline osp::expected<std::uint32_t, Error> BuildSubscribeAckDatagram(
    osp::span<std::uint8_t> out, const SubscribeAckRequest& request,
    sd::SdSession session) noexcept {
  if (request.ttl > sd::kMaxTtl) {
    return osp::expected<std::uint32_t, Error>::error(Error::InvalidTtl(request.ttl));
  }
  sd::EventGroupEntry payload;
  payload.options_count = sd::OptionsCount(0U, 0U);
  payload.service_id = request.service_id;
  payload.instance_id = request.instance_id;
  payload.major_version = request.major_version;
  payload.ttl = request.ttl;
  payload.counter = request.counter;
  payload.event_group_id = request.event_group_id;
  const sd::Entry entries[1] = {sd::Entry::Ack(payload)};
  const sd::Options no_options[1] = {
      sd::Options::Configuration(osp::span<const std::uint8_t>())};
  return detail::WriteDatagram(out, osp::make_span(entries),
                               osp::span<const sd::Options>(no_options, 0U), session);
}

namespace detail {

inline osp::expected<std::uint32_t, Error> WriteDatagram(
    osp::span<std::uint8_t> out, osp::span<const sd::Entry> entries,
    osp::span<const sd::Options> options, sd::SdSession session) noexcept {
  const sd::Header sd_header(sd::Flags::NewSd(session.reboot), entries, options);
  const std::uint32_t sd_size = sd_header.EncodedSize();
  const std::uint32_t total = kHeaderSize + sd_size;
  if (out.size() < total) {
    return osp::expected<std::uint32_t, Error>::error(
        Error::InsufficientBuffer(total, static_cast<std::uint32_t>(out.size())));
  }
  const Header wrapper = Header::NewSd(session.id, sd_size);
  auto header_result = wrapper.EncodeTo(osp::span<std::uint8_t>(out.data(), kHeaderSize));
  if (!header_result.has_value()) {
    return osp::expected<std::uint32_t, Error>::error(header_result.get_error());
  }
  auto body_result = sd_header.EncodeTo(osp::span<std::uint8_t>(
      out.data() + kHeaderSize, out.size() - kHeaderSize));
  if (!body_result.has_value()) {
    return osp::expected<std::uint32_t, Error>::error(body_result.get_error());
  }
  return osp::expected<std::uint32_t, Error>::success(total);
}

inline osp::expected<std::uint32_t, Error> BuildServiceDatagram(
    osp::span<std::uint8_t> out, const OfferServiceRequest& request,
    sd::SdSession session, bool stop) noexcept {
  if (request.ttl > sd::kMaxTtl) {
    return osp::expected<std::uint32_t, Error>::error(Error::InvalidTtl(request.ttl));
  }
  sd::ServiceEntry payload;
  payload.options_count = sd::OptionsCount(1U, 0U);
  payload.service_id = request.service_id;
  payload.instance_id = request.instance_id;
  payload.major_version = request.major_version;
  payload.ttl = request.ttl;
  payload.minor_version = request.minor_version;
  const sd::Entry entries[1] = {stop ? sd::Entry::StopOffer(payload)
                                     : sd::Entry::Offer(payload)};
  const sd::Options options[1] = {
      sd::Options::IpV4Endpoint(request.local_ip, sd::TransportProtocol::kUdp,
                                request.unicast_port)};
  return WriteDatagram(out, osp::make_span(entries), osp::make_span(options), session);
}

}  // namespace detail

}  // namespace osp::someip

#endif  // OSP_SOMEIP_SD_CODEC_HPP_
