/**
 * @file fragmented_demo.cpp
 * @brief Sends one E2E-protected request as SOME/IP-TP segments and checks the
 *        protected reply. No sockets: the sink keeps the reply bytes.
 */

#include "osp/someip/e2e_profile1.hpp"
#include "osp/someip/server.hpp"
#include "osp/someip/tp.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

constexpr std::uint16_t kDataId = 0x1234U;
using Profile = osp::someip::E2EProfile1<kDataId>;

struct CaptureSink final {
  std::uint8_t bytes[512] = {};
  std::size_t size{0U};

  void operator()(osp::span<const std::uint8_t> datagram,
                  const osp::someip::Ipv4Endpoint& /*to*/) noexcept {
    size = datagram.size();
    for (std::size_t index = 0U; index < datagram.size(); ++index) {
      bytes[index] = datagram[index];
    }
  }
};

struct EchoHandler final {
  osp::someip::RequestReply OnRequest(const osp::someip::IncomingRequest& request) noexcept {
    return osp::someip::RequestReply{true, false, osp::someip::ReturnCode::Ok(), request.payload};
  }
};

constexpr std::array<std::uint8_t, 8U> kUpperHeader{0U, 1U, 0U, 1U, 1U, 1U, 0U, 0U};

/// @brief Encode one TP segment of a request.
std::uint32_t BuildSegment(std::uint8_t* out, std::uint32_t offset, bool more,
                           osp::span<const std::uint8_t> data) noexcept {
  const osp::someip::Header header = osp::someip::Header::New(
      osp::someip::MessageId::FromServiceAndMethod(0x1234U, 0x0001U), 0x00010001U, 0x01U, 0x01U,
      osp::someip::MessageTypeField::New(osp::someip::MessageType::kRequest, true),
      osp::someip::ReturnCode::Ok(),
      static_cast<std::uint32_t>(osp::someip::TpHeader::kSize + data.size()));
  (void)header.EncodeTo(osp::make_span(out, osp::someip::kHeaderSize));
  (void)osp::someip::TpHeader::Write(
      osp::make_span(out + osp::someip::kHeaderSize, osp::someip::TpHeader::kSize), offset, more);
  std::uint8_t* target = out + osp::someip::kHeaderSize + osp::someip::TpHeader::kSize;
  for (std::size_t index = 0U; index < data.size(); ++index) {
    target[index] = data[index];
  }
  return osp::someip::kHeaderSize + osp::someip::TpHeader::kSize +
         static_cast<std::uint32_t>(data.size());
}

}  // namespace

int main() {
  constexpr std::size_t kPayloadSize = 300U;
  std::array<std::uint8_t, kPayloadSize> payload{};
  for (std::size_t index = 0U; index < payload.size(); ++index) {
    payload[index] = static_cast<std::uint8_t>(index);
  }

  Profile sender;
  Profile receiver;
  std::array<std::uint8_t, kPayloadSize + Profile::kHeaderSize> protected_payload{};
  for (std::size_t index = 0U; index < payload.size(); ++index) {
    protected_payload[index] = payload[index];
  }
  const auto body = sender.Protect(osp::someip::MessageId(0x12340001U), kUpperHeader,
                                   osp::span<std::uint8_t>(protected_payload.data(), protected_payload.size()),
                                   payload.size());

  CaptureSink sink;
  osp::someip::Server<CaptureSink, 2U, 2U, 512U, EchoHandler, Profile,
                      osp::someip::NoServiceObserver, 2U,
                      osp::someip::TpReassembly<512U, 1U, 1000U>>
      server(sink);
  const osp::someip::Ipv4Endpoint peer{{127U, 0U, 0U, 1U}, 30510U};

  // Segments are 16-byte aligned except the last one, as TP requires.
  std::uint8_t datagram[128] = {};
  std::uint32_t segments = 0U;
  for (std::size_t offset = 0U; offset < body.size(); offset += 16U) {
    const std::size_t length =
        (body.size() - offset < 16U) ? (body.size() - offset) : 16U;
    const bool more = (offset + length) < body.size();
    const std::uint32_t size = BuildSegment(
        datagram, static_cast<std::uint32_t>(offset), more,
        osp::span<const std::uint8_t>(body.data() + offset, length));
    server.HandleDatagram(osp::span<const std::uint8_t>(datagram, size), peer,
                          static_cast<std::uint32_t>(offset));
    ++segments;
  }

  if (0U == sink.size) {
    std::printf("no reply after %u segments\n", static_cast<unsigned>(segments));
    return 1;
  }
  const auto reply = osp::someip::MessageView::Parse(
      osp::span<const std::uint8_t>(sink.bytes, sink.size));
  if (!reply.has_value()) {
    std::printf("reply does not parse\n");
    return 1;
  }
  const auto check =
      receiver.Check(reply.value().Head().Id(), kUpperHeader, reply.value().Body());
  if (!check.ok || check.payload.size() != payload.size()) {
    std::printf("reply failed the E2E check\n");
    return 1;
  }
  for (std::size_t index = 0U; index < payload.size(); ++index) {
    if (check.payload[index] != payload[index]) {
      std::printf("payload differs at byte %u\n", static_cast<unsigned>(index));
      return 1;
    }
  }

  std::printf("segments=%u request=%u bytes reply=%u bytes payload=%u bytes: ok\n",
              static_cast<unsigned>(segments),
              static_cast<unsigned>(body.size()),
              static_cast<unsigned>(sink.size),
              static_cast<unsigned>(check.payload.size()));
  return 0;
}
