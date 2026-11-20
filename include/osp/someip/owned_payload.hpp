/**
 * @file owned_payload.hpp
 * @brief Fixed-capacity payload that owns its bytes.
 *
 * The counterpart of OpaquePayload, which borrows the caller's buffer. Use
 * OwnedPayload when the payload must outlive the input (queued requests,
 * deferred responses); capacity is a template parameter, so nothing allocates.
 */

#ifndef OSP_SOMEIP_OWNED_PAYLOAD_HPP_
#define OSP_SOMEIP_OWNED_PAYLOAD_HPP_

#include "osp/someip/error.hpp"
#include "osp/someip/message_id.hpp"
#include "osp/someip/payload.hpp"
#include "osp/someip/wire.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <cstddef>
#include <cstdint>

namespace osp::someip {

/**
 * @brief Up to @p Capacity payload bytes stored inline.
 *
 * @tparam Capacity Maximum payload size in bytes.
 */
template <std::uint32_t Capacity>
class OwnedPayload final : public PayloadCodec<OwnedPayload<Capacity>> {
 public:
  constexpr OwnedPayload() noexcept = default;

  /// @brief Copy @p bytes into the payload; fails when they exceed Capacity.
  static osp::expected<OwnedPayload, Error> FromPayloadBytes(
      MessageId /*id*/, osp::span<const std::uint8_t> bytes) noexcept {
    OwnedPayload payload;
    if (bytes.size() > Capacity) {
      return osp::expected<OwnedPayload, Error>::error(Error::InsufficientBuffer(
          Capacity, static_cast<std::uint32_t>(bytes.size())));
    }
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
      payload.data_[index] = bytes[index];
    }
    payload.size_ = static_cast<std::uint32_t>(bytes.size());
    return osp::expected<OwnedPayload, Error>::success(payload);
  }

  constexpr osp::span<const std::uint8_t> Bytes() const noexcept {
    return osp::span<const std::uint8_t>(data_, size_);
  }
  constexpr std::uint32_t Size() const noexcept { return size_; }

  std::uint32_t DoEncodedSize() const noexcept { return size_; }

  osp::expected<std::uint32_t, Error> DoEncodeTo(
      osp::span<std::uint8_t> out) const noexcept {
    ByteWriter writer(out);
    if (!writer.WriteBytes(Bytes())) {
      return osp::expected<std::uint32_t, Error>::error(
          Error::InsufficientBuffer(size_, static_cast<std::uint32_t>(out.size())));
    }
    return osp::expected<std::uint32_t, Error>::success(
        static_cast<std::uint32_t>(writer.Position()));
  }

 private:
  std::uint8_t data_[Capacity]{};
  std::uint32_t size_{0U};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_OWNED_PAYLOAD_HPP_
