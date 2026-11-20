/**
 * @file e2e_profile4.hpp
 * @brief AUTOSAR E2E Profile 4: CRC-32 plus a 16-bit counter and data ID.
 *
 * The protected payload starts with a 12-byte header: the total length, the
 * counter, the data ID (instance in the high byte) and the CRC-32. The CRC
 * covers the first eight header bytes followed by everything after the CRC.
 *
 * CRC-32/AUTOSAR: polynomial 0xF4ACFB13 (reflected 0xC8DF352F), initial value
 * and final XOR 0xFFFFFFFF, reflected input and output. The catalogue check
 * value over "123456789" is 0x1697D06A.
 */

#ifndef OSP_SOMEIP_E2E_PROFILE4_HPP_
#define OSP_SOMEIP_E2E_PROFILE4_HPP_

#include "osp/someip/e2e.hpp"
#include "osp/someip/message_id.hpp"
#include "osp/someip/wire.hpp"
#include "osp/span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace osp::someip {

/// @brief CRC-32/AUTOSAR over a byte range.
class Crc32Autosar final {
 public:
  void Update(std::uint8_t byte) noexcept {
    crc_ ^= byte;
    for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
      const bool low = 0U != (crc_ & 1U);
      crc_ >>= 1U;
      if (low) {
        crc_ ^= kReflectedPolynomial;
      }
    }
  }

  void Update(osp::span<const std::uint8_t> bytes) noexcept {
    for (const std::uint8_t byte : bytes) {
      Update(byte);
    }
  }

  std::uint32_t Value() const noexcept { return crc_ ^ 0xFFFFFFFFU; }

 private:
  static constexpr std::uint32_t kReflectedPolynomial = 0xC8DF352FU;  // reflect32(0xF4ACFB13)
  std::uint32_t crc_{0xFFFFFFFFU};
};

/**
 * @brief E2E Profile 4 policy bound to one data ID and instance.
 *
 * @tparam DataId         16-bit data ID carried in the header.
 * @tparam Instance       Instance identifier, 1..255; stored in the high byte.
 * @tparam MaxDeltaCounter Largest accepted gap between counter values.
 *
 * Protect() prepends the 12-byte header, so the caller must pass a buffer with
 * twelve bytes of spare room.
 */
template <std::uint16_t DataId, std::uint8_t Instance = 1U,
          std::uint16_t MaxDeltaCounter = 15U>
class E2EProfile4 final {
 public:
  static constexpr std::uint32_t kHeaderSize = 12U;
  static constexpr std::uint16_t kCounterMask = 0xFFFFU;

  E2ECheck Check(MessageId /*id*/,
                 const std::array<std::uint8_t, 8U>& /*upper_header*/,
                 osp::span<const std::uint8_t> payload) noexcept {
    E2ECheck result;
    result.applies = true;
    if (payload.size() < kHeaderSize) {
      return Fail(result);
    }
    const std::uint32_t length = ReadU16(payload, 0U);
    if (length != payload.size() || ReadU32(payload, 4U) != kDataIdWord) {
      return Fail(result);
    }
    if (ReadU32(payload, 8U) != ComputeCrc(payload)) {
      return Fail(result);
    }
    const std::uint16_t counter = ReadU16(payload, 2U);
    if (has_last_) {
      const std::uint16_t delta = static_cast<std::uint16_t>((counter - last_) & kCounterMask);
      if (0U == delta || delta > MaxDeltaCounter) {
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
    if (buffer.size() < total || total > 0xFFFFU) {
      return osp::span<std::uint8_t>();
    }
    std::memmove(buffer.data() + kHeaderSize, buffer.data(), payload_size);
    WriteU16(buffer, 0U, static_cast<std::uint16_t>(total));
    WriteU16(buffer, 2U, counter_);
    WriteU32(buffer, 4U, kDataIdWord);
    WriteU32(buffer, 8U, 0U);
    WriteU32(buffer, 8U, ComputeCrc(osp::span<const std::uint8_t>(buffer.data(), total)));
    counter_ = static_cast<std::uint16_t>((counter_ + 1U) & kCounterMask);
    return osp::span<std::uint8_t>(buffer.data(), total);
  }

 private:
  static constexpr std::uint32_t kDataIdWord =
      (static_cast<std::uint32_t>(Instance) << 24U) | static_cast<std::uint32_t>(DataId);

  static E2ECheck Fail(E2ECheck result) noexcept {
    result.ok = false;
    result.status = E2EStatus::kError;
    return result;
  }

  static std::uint32_t ComputeCrc(osp::span<const std::uint8_t> protected_payload) noexcept {
    Crc32Autosar crc;
    crc.Update(osp::span<const std::uint8_t>(protected_payload.data(), 8U));
    if (protected_payload.size() > kHeaderSize) {
      crc.Update(osp::span<const std::uint8_t>(
          protected_payload.data() + kHeaderSize, protected_payload.size() - kHeaderSize));
    }
    return crc.Value();
  }

  static std::uint16_t ReadU16(osp::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[at]) << 8U) |
                                      static_cast<std::uint16_t>(bytes[at + 1U]));
  }

  static std::uint32_t ReadU32(osp::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return (static_cast<std::uint32_t>(bytes[at]) << 24U) |
           (static_cast<std::uint32_t>(bytes[at + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[at + 3U]);
  }

  static void WriteU16(osp::span<std::uint8_t> bytes, std::size_t at, std::uint16_t value) noexcept {
    bytes[at] = static_cast<std::uint8_t>(value >> 8U);
    bytes[at + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
  }

  static void WriteU32(osp::span<std::uint8_t> bytes, std::size_t at, std::uint32_t value) noexcept {
    bytes[at] = static_cast<std::uint8_t>(value >> 24U);
    bytes[at + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[at + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[at + 3U] = static_cast<std::uint8_t>(value & 0xFFU);
  }

  std::uint16_t counter_{0U};
  std::uint16_t last_{0U};
  bool has_last_{false};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_E2E_PROFILE4_HPP_
