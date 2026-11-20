/**
 * @file e2e_profile5.hpp
 * @brief AUTOSAR E2E Profile 5: CRC-16 plus an 8-bit counter.
 *
 * The protected payload starts with three bytes: a CRC-16 followed by the
 * counter. The CRC covers the counter and the payload, then the 16-bit data ID
 * appended little-endian. There is no length field; the receiver validates the
 * CRC and the counter window only.
 *
 * CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no final XOR,
 * most significant bit first. The catalogue check value over "123456789" is
 * 0x29B1.
 */

#ifndef OSP_SOMEIP_E2E_PROFILE5_HPP_
#define OSP_SOMEIP_E2E_PROFILE5_HPP_

#include "osp/someip/e2e.hpp"
#include "osp/someip/message_id.hpp"
#include "osp/span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace osp::someip {

/// @brief CRC-16/CCITT-FALSE, the checksum used by E2E Profile 5.
class Crc16CcittFalse final {
 public:
  void Update(std::uint8_t byte) noexcept {
    crc_ = static_cast<std::uint16_t>(crc_ ^ (static_cast<std::uint16_t>(byte) << 8U));
    for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
      const bool high = 0U != (crc_ & 0x8000U);
      crc_ = static_cast<std::uint16_t>(crc_ << 1U);
      if (high) {
        crc_ = static_cast<std::uint16_t>(crc_ ^ 0x1021U);
      }
    }
  }

  void Update(osp::span<const std::uint8_t> bytes) noexcept {
    for (const std::uint8_t byte : bytes) {
      Update(byte);
    }
  }

  std::uint16_t Value() const noexcept { return crc_; }

 private:
  std::uint16_t crc_{0xFFFFU};
};

/**
 * @brief E2E Profile 5 policy bound to one data ID.
 *
 * @tparam DataId         16-bit data ID appended to the CRC input.
 * @tparam MaxDeltaCounter Largest accepted gap between counter values.
 *
 * Protect() prepends the three-byte header, so the caller must pass a buffer
 * with three bytes of spare room. A repeated counter (delta zero) is accepted,
 * as in the reference implementation.
 */
template <std::uint16_t DataId, std::uint16_t MaxDeltaCounter = 15U>
class E2EProfile5 final {
 public:
  static constexpr std::uint32_t kHeaderSize = 3U;

  E2ECheck Check(MessageId /*id*/,
                 const std::array<std::uint8_t, 8U>& /*upper_header*/,
                 osp::span<const std::uint8_t> payload) noexcept {
    E2ECheck result;
    result.applies = true;
    if (payload.size() < kHeaderSize) {
      return Fail(result);
    }
    const std::uint16_t received = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(payload[0U]) << 8U) | payload[1U]);
    if (received != ComputeCrc(payload)) {
      return Fail(result);
    }
    const std::uint8_t counter = payload[2U];
    if (has_last_) {
      const std::uint16_t delta = static_cast<std::uint8_t>(counter - last_);
      if (delta > MaxDeltaCounter) {
        return Fail(result);
      }
    }
    last_ = counter;
    has_last_ = true;
    result.payload =
        osp::span<const std::uint8_t>(payload.data() + kHeaderSize, payload.size() - kHeaderSize);
    return result;
  }

  osp::span<std::uint8_t> Protect(MessageId /*id*/,
                                  const std::array<std::uint8_t, 8U>& /*upper_header*/,
                                  osp::span<std::uint8_t> buffer,
                                  std::uint32_t payload_size) noexcept {
    const std::uint32_t total = payload_size + kHeaderSize;
    if (buffer.size() < total) {
      return osp::span<std::uint8_t>();
    }
    std::memmove(buffer.data() + kHeaderSize, buffer.data(), payload_size);
    buffer[0U] = 0U;
    buffer[1U] = 0U;
    buffer[2U] = counter_;
    const std::uint16_t crc =
        ComputeCrc(osp::span<const std::uint8_t>(buffer.data(), total));
    buffer[0U] = static_cast<std::uint8_t>(crc >> 8U);
    buffer[1U] = static_cast<std::uint8_t>(crc & 0xFFU);
    ++counter_;
    return osp::span<std::uint8_t>(buffer.data(), total);
  }

 private:
  static E2ECheck Fail(E2ECheck result) noexcept {
    result.ok = false;
    result.status = E2EStatus::kError;
    return result;
  }

  static std::uint16_t ComputeCrc(osp::span<const std::uint8_t> protected_payload) noexcept {
    Crc16CcittFalse crc;
    crc.Update(osp::span<const std::uint8_t>(protected_payload.data() + 2U,
                                             protected_payload.size() - 2U));
    const std::array<std::uint8_t, 2U> data_id{
        static_cast<std::uint8_t>(DataId & 0xFFU), static_cast<std::uint8_t>(DataId >> 8U)};
    crc.Update(osp::span<const std::uint8_t>(data_id.data(), data_id.size()));
    return crc.Value();
  }

  std::uint8_t counter_{0U};
  std::uint8_t last_{0U};
  bool has_last_{false};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_E2E_PROFILE5_HPP_
