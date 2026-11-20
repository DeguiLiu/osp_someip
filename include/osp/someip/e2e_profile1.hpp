/**
 * @file e2e_profile1.hpp
 * @brief AUTOSAR E2E Profile 1: CRC-8 SAE J1850 plus a 4-bit counter.
 *
 * On the wire the protected payload starts with two bytes: the counter in the
 * low nibble of the first byte, and the CRC in the second. The CRC covers the
 * 16-bit data ID followed by the whole protected payload with the CRC byte
 * read as zero. Protect() prepends the header, so the caller must pass a
 * buffer with two bytes of spare room.
 */

#ifndef OSP_SOMEIP_E2E_PROFILE1_HPP_
#define OSP_SOMEIP_E2E_PROFILE1_HPP_

#include "osp/someip/e2e.hpp"
#include "osp/someip/message_id.hpp"
#include "osp/span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace osp::someip {

/// @brief CRC-8/SAE-J1850: poly 0x1D, init 0xFF, final XOR 0xFF.
class Crc8SaeJ1850 final {
 public:
  void Update(std::uint8_t byte) noexcept {
    crc_ = static_cast<std::uint8_t>(crc_ ^ byte);
    for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
      const bool high = 0U != (crc_ & 0x80U);
      crc_ = static_cast<std::uint8_t>(crc_ << 1U);
      if (high) {
        crc_ = static_cast<std::uint8_t>(crc_ ^ 0x1DU);
      }
    }
  }

  void Update(osp::span<const std::uint8_t> bytes) noexcept {
    for (const std::uint8_t byte : bytes) {
      Update(byte);
    }
  }

  std::uint8_t Value() const noexcept { return static_cast<std::uint8_t>(crc_ ^ 0xFFU); }

 private:
  std::uint8_t crc_{0xFFU};
};

/// @brief E2E Profile 1 policy bound to one data ID.
template <std::uint16_t DataId>
class E2EProfile1 final {
 public:
  static constexpr std::uint32_t kHeaderSize = 2U;
  static constexpr std::uint8_t kCounterMask = 0x0FU;

  E2ECheck Check(MessageId /*id*/,
                 const std::array<std::uint8_t, 8U>& /*upper_header*/,
                 osp::span<const std::uint8_t> payload) noexcept {
    E2ECheck result;
    result.applies = true;
    if (payload.size() < kHeaderSize) {
      result.ok = false;
      result.status = E2EStatus::kError;
      return result;
    }
    const std::uint8_t counter = static_cast<std::uint8_t>(payload[0U] & kCounterMask);
    if (has_last_ && (0U == ((counter - last_) & kCounterMask))) {
      result.ok = false;
      result.status = E2EStatus::kError;
      return result;
    }
    if (payload[1U] != ComputeCrc(counter, payload)) {
      result.ok = false;
      result.status = E2EStatus::kError;
      return result;
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
    if (buffer.size() < (payload_size + kHeaderSize)) {
      return osp::span<std::uint8_t>();
    }
    std::memmove(buffer.data() + kHeaderSize, buffer.data(), payload_size);
    buffer[0U] = counter_;
    buffer[1U] = ComputeCrc(
        counter_, osp::span<const std::uint8_t>(buffer.data(), payload_size + kHeaderSize));
    counter_ = static_cast<std::uint8_t>((counter_ + 1U) & kCounterMask);
    return osp::span<std::uint8_t>(buffer.data(), payload_size + kHeaderSize);
  }

 private:
  static constexpr std::array<std::uint8_t, 2U> kDataIdBytes{
      static_cast<std::uint8_t>(DataId >> 8U), static_cast<std::uint8_t>(DataId & 0xFFU)};

  /// @brief CRC over data ID, counter, a zero CRC byte, then the data.
  static std::uint8_t ComputeCrc(std::uint8_t counter,
                                 osp::span<const std::uint8_t> protected_payload) noexcept {
    Crc8SaeJ1850 crc;
    crc.Update(osp::span<const std::uint8_t>(kDataIdBytes.data(), kDataIdBytes.size()));
    crc.Update(counter);
    crc.Update(0U);
    crc.Update(osp::span<const std::uint8_t>(protected_payload.data() + kHeaderSize,
                                             protected_payload.size() - kHeaderSize));
    return crc.Value();
  }

  std::uint8_t counter_{0U};
  std::uint8_t last_{0U};
  bool has_last_{false};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_E2E_PROFILE1_HPP_
