/**
 * @file e2e_profile7.hpp
 * @brief AUTOSAR E2E Profile 7: CRC-64 plus a 32-bit counter and data ID.
 *
 * The protected payload starts with a 20-byte header: the CRC-64, the total
 * length, the counter and the data ID. The CRC covers everything after the
 * eight CRC bytes, so the length, counter, data ID and payload are protected.
 *
 * CRC-64/XZ: polynomial 0x42F0E1EBA9EA3693 (reflected 0xC96C5795D7870F42),
 * initial value and final XOR 0xFFFFFFFFFFFFFFFF. The catalogue check value
 * over "123456789" is 0x995DC9BBDF1939FA.
 */

#ifndef OSP_SOMEIP_E2E_PROFILE7_HPP_
#define OSP_SOMEIP_E2E_PROFILE7_HPP_

#include "osp/someip/e2e.hpp"
#include "osp/someip/message_id.hpp"
#include "osp/span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace osp::someip {

/// @brief CRC-64/XZ, the checksum used by E2E Profile 7.
class Crc64Xz final {
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

  std::uint64_t Value() const noexcept { return crc_ ^ 0xFFFFFFFFFFFFFFFFULL; }

 private:
  static constexpr std::uint64_t kReflectedPolynomial = 0xC96C5795D7870F42ULL;
  std::uint64_t crc_{0xFFFFFFFFFFFFFFFFULL};
};

/**
 * @brief E2E Profile 7 policy bound to one data ID.
 *
 * @tparam DataId         32-bit data ID carried in the header.
 * @tparam MaxDeltaCounter Largest accepted gap between counter values.
 *
 * Protect() prepends the 20-byte header, so the caller must pass a buffer with
 * twenty bytes of spare room. A repeated counter (delta zero) is accepted, as
 * in the reference implementation, because it means the same message was sent
 * twice rather than a loss of sequence.
 */
template <std::uint32_t DataId, std::uint32_t MaxDeltaCounter = 15U>
class E2EProfile7 final {
 public:
  static constexpr std::uint32_t kHeaderSize = 20U;

  E2ECheck Check(MessageId /*id*/,
                 const std::array<std::uint8_t, 8U>& /*upper_header*/,
                 osp::span<const std::uint8_t> payload) noexcept {
    E2ECheck result;
    result.applies = true;
    if (payload.size() < kHeaderSize || payload.size() > 0xFFFFFFFFU) {
      return Fail(result);
    }
    const std::uint32_t length = ReadU32(payload, 8U);
    if (length != payload.size() || ReadU32(payload, 16U) != DataId) {
      return Fail(result);
    }
    if (ReadU64(payload, 0U) != ComputeCrc(payload)) {
      return Fail(result);
    }
    const std::uint32_t counter = ReadU32(payload, 12U);
    if (has_last_) {
      const std::uint32_t delta = counter - last_;
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
    WriteU32(buffer, 8U, total);
    WriteU32(buffer, 12U, counter_);
    WriteU32(buffer, 16U, DataId);
    WriteU64(buffer, 0U, 0U);
    WriteU64(buffer, 0U, ComputeCrc(osp::span<const std::uint8_t>(buffer.data(), total)));
    ++counter_;
    return osp::span<std::uint8_t>(buffer.data(), total);
  }

 private:
  static E2ECheck Fail(E2ECheck result) noexcept {
    result.ok = false;
    result.status = E2EStatus::kError;
    return result;
  }

  static std::uint64_t ComputeCrc(osp::span<const std::uint8_t> protected_payload) noexcept {
    Crc64Xz crc;
    crc.Update(osp::span<const std::uint8_t>(protected_payload.data() + 8U,
                                             protected_payload.size() - 8U));
    return crc.Value();
  }

  static std::uint32_t ReadU32(osp::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return (static_cast<std::uint32_t>(bytes[at]) << 24U) |
           (static_cast<std::uint32_t>(bytes[at + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[at + 3U]);
  }

  static std::uint64_t ReadU64(osp::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
      value = (value << 8U) | bytes[at + index];
    }
    return value;
  }

  static void WriteU32(osp::span<std::uint8_t> bytes, std::size_t at,
                       std::uint32_t value) noexcept {
    bytes[at] = static_cast<std::uint8_t>(value >> 24U);
    bytes[at + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[at + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[at + 3U] = static_cast<std::uint8_t>(value & 0xFFU);
  }

  static void WriteU64(osp::span<std::uint8_t> bytes, std::size_t at,
                       std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < 8U; ++index) {
      bytes[at + index] = static_cast<std::uint8_t>(value >> ((7U - index) * 8U));
    }
  }

  std::uint32_t counter_{0U};
  std::uint32_t last_{0U};
  bool has_last_{false};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_E2E_PROFILE7_HPP_
