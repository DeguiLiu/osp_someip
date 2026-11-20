/**
 * @file payload.hpp
 * @brief Payload codec skeleton and the opaque (raw byte) payload type.
 */

#ifndef OSP_SOMEIP_PAYLOAD_HPP_
#define OSP_SOMEIP_PAYLOAD_HPP_

#include "osp/someip/error.hpp"
#include "osp/someip/message_id.hpp"
#include "osp/someip/wire.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <cstdint>

namespace osp::someip {

/**
 * @brief CRTP payload skeleton.
 *
 * @tparam Derived supplies `DoEncodedSize()` and
 *                `DoEncodeTo(osp::span<std::uint8_t>)`.
 */
template <typename Derived>
class PayloadCodec {
 public:
  std::uint32_t EncodedSize() const noexcept {
    return static_cast<const Derived*>(this)->DoEncodedSize();
  }

  osp::expected<std::uint32_t, Error> EncodeTo(osp::span<std::uint8_t> out) const noexcept {
    return static_cast<const Derived*>(this)->DoEncodeTo(out);
  }

 protected:
  PayloadCodec() = default;
  PayloadCodec(const PayloadCodec&) = default;
  PayloadCodec(PayloadCodec&&) = default;
  PayloadCodec& operator=(const PayloadCodec&) = default;
  PayloadCodec& operator=(PayloadCodec&&) = default;
  ~PayloadCodec() = default;
};

/**
 * @brief Opaque payload: the bytes are copied verbatim, no schema.
 *
 * The view is non-owning; the caller keeps the backing buffer alive for as
 * long as the payload is encoded.
 */
class OpaquePayload final : public PayloadCodec<OpaquePayload> {
 public:
  constexpr OpaquePayload() noexcept = default;
  explicit constexpr OpaquePayload(osp::span<const std::uint8_t> bytes) noexcept
      : bytes_(bytes) {}

  /// @brief Mirror of the payload-type factory: any bytes belong to the ID.
  static osp::expected<OpaquePayload, Error> FromPayloadBytes(
      MessageId /*id*/, osp::span<const std::uint8_t> bytes) noexcept {
    return osp::expected<OpaquePayload, Error>::success(OpaquePayload(bytes));
  }

  constexpr osp::span<const std::uint8_t> Bytes() const noexcept { return bytes_; }

  std::uint32_t DoEncodedSize() const noexcept {
    return static_cast<std::uint32_t>(bytes_.size());
  }

  osp::expected<std::uint32_t, Error> DoEncodeTo(
      osp::span<std::uint8_t> out) const noexcept {
    ByteWriter writer(out);
    if (!writer.WriteBytes(bytes_)) {
      return osp::expected<std::uint32_t, Error>::error(
          Error::InsufficientBuffer(DoEncodedSize(), static_cast<std::uint32_t>(out.size())));
    }
    return osp::expected<std::uint32_t, Error>::success(static_cast<std::uint32_t>(
        writer.Position()));
  }

 private:
  osp::span<const std::uint8_t> bytes_{};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_PAYLOAD_HPP_
