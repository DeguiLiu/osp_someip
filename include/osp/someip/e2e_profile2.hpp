/**
 * @file e2e_profile2.hpp
 * @brief AUTOSAR E2E Profile 2: CRC-8/H2F plus a 4-bit counter and a data ID list.
 *
 * The first two bytes of the protected data are the header: the CRC in byte 0
 * and the counter in the low nibble of byte 1. The CRC covers bytes 1..end of
 * the data followed by the one-byte data ID selected by the counter.
 *
 * Unlike the other profiles, Profile 2 is checked per communication cycle: the
 * receiver keeps a small state machine and needs to know when a cycle passed
 * without data. Call Cycle() once per cycle for that case; Check() itself means
 * "data arrived".
 *
 * CRC-8/H2F: polynomial 0x2F, initial value and final XOR 0xFF, most significant
 * bit first. The catalogue check value over "123456789" is 0xDF.
 */

#ifndef OSP_SOMEIP_E2E_PROFILE2_HPP_
#define OSP_SOMEIP_E2E_PROFILE2_HPP_

#include "osp/someip/e2e.hpp"
#include "osp/someip/message_id.hpp"
#include "osp/span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace osp::someip {

/// @brief CRC-8/H2F, the checksum used by E2E Profile 2.
class Crc8H2f final {
 public:
  void Update(std::uint8_t byte) noexcept {
    crc_ = static_cast<std::uint8_t>(crc_ ^ byte);
    for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
      const bool high = 0U != (crc_ & 0x80U);
      crc_ = static_cast<std::uint8_t>(crc_ << 1U);
      if (high) {
        crc_ = static_cast<std::uint8_t>(crc_ ^ 0x2FU);
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

/// @brief Sixteen data IDs for E2EProfile2, one per counter value.
template <std::uint8_t First>
struct SequentialP2Ids final {
  static constexpr std::array<std::uint8_t, 16U> kDataIds{
      First,      static_cast<std::uint8_t>(First + 1U),  static_cast<std::uint8_t>(First + 2U),
      static_cast<std::uint8_t>(First + 3U), static_cast<std::uint8_t>(First + 4U),
      static_cast<std::uint8_t>(First + 5U), static_cast<std::uint8_t>(First + 6U),
      static_cast<std::uint8_t>(First + 7U), static_cast<std::uint8_t>(First + 8U),
      static_cast<std::uint8_t>(First + 9U), static_cast<std::uint8_t>(First + 10U),
      static_cast<std::uint8_t>(First + 11U), static_cast<std::uint8_t>(First + 12U),
      static_cast<std::uint8_t>(First + 13U), static_cast<std::uint8_t>(First + 14U),
      static_cast<std::uint8_t>(First + 15U)};
};

/**
 * @brief E2E Profile 2 policy.
 *
 * @tparam Ids                     Type with `static constexpr std::array<uint8_t, 16> kDataIds`.
 * @tparam MaxDeltaCounterInit     Initial counter window.
 * @tparam MaxNoNewOrRepeatedData  Repeated or missing messages tolerated before resync.
 * @tparam SyncCounterInit         Valid messages required to leave the sync state.
 *
 * Protect() writes the two-byte header in place, so the caller must pass a
 * buffer whose first two bytes are reserved for it.
 */
template <typename Ids, std::uint8_t MaxDeltaCounterInit = 15U,
          std::uint8_t MaxNoNewOrRepeatedData = 15U, std::uint8_t SyncCounterInit = 1U>
class E2EProfile2 final {
 public:
  static constexpr std::uint32_t kHeaderSize = 2U;

  E2ECheck Check(MessageId /*id*/,
                 const std::array<std::uint8_t, 8U>& /*upper_header*/,
                 osp::span<const std::uint8_t> payload) noexcept {
    E2ECheck result;
    result.applies = true;
    if (payload.size() < kHeaderSize) {
      return Fail(result, E2EStatus::kError);
    }
    result.payload =
        osp::span<const std::uint8_t>(payload.data() + kHeaderSize, payload.size() - kHeaderSize);
    if (max_delta_counter_ < 15U) {
      ++max_delta_counter_;
    }
    const std::uint8_t counter = static_cast<std::uint8_t>(payload[1U] & 0x0FU);
    if (payload[0U] != ComputeCrc(payload, counter)) {
      return Fail(result, E2EStatus::kError);
    }
    if (wait_for_first_) {
      wait_for_first_ = false;
      max_delta_counter_ = MaxDeltaCounterInit;
      last_valid_counter_ = counter;
      lost_data_ = 0U;
      return Deliver(result, E2EStatus::kOk);
    }
    const std::uint8_t delta =
        static_cast<std::uint8_t>((counter - last_valid_counter_) & 0x0FU);
    return Analyse(delta, counter, result);
  }

  /// @brief Advance one cycle in which no data arrived.
  void Cycle() noexcept {
    if (max_delta_counter_ < 15U) {
      ++max_delta_counter_;
    }
    if (no_new_or_repeated_ < 15U) {
      ++no_new_or_repeated_;
    }
    status_ = E2EStatus::kNoData;
  }

  /// @brief Protect in place; the first two bytes of the span must be reserved.
  osp::span<std::uint8_t> Protect(MessageId /*id*/,
                                  const std::array<std::uint8_t, 8U>& /*upper_header*/,
                                  osp::span<std::uint8_t> buffer,
                                  std::uint32_t payload_size) noexcept {
    const std::uint32_t total = payload_size + kHeaderSize;
    if (buffer.size() < total) {
      return osp::span<std::uint8_t>();
    }
    std::memmove(buffer.data() + kHeaderSize, buffer.data(), payload_size);
    counter_ = static_cast<std::uint8_t>((counter_ + 1U) & 0x0FU);
    // The application payload starts at byte 2, so the high nibble is reserved.
    buffer[1U] = counter_;
    buffer[0U] = ComputeCrc(osp::span<const std::uint8_t>(buffer.data(), total), counter_);
    return osp::span<std::uint8_t>(buffer.data(), total);
  }

 private:
  E2ECheck Deliver(E2ECheck result, E2EStatus status) noexcept {
    result.ok = true;
    result.status = status;
    status_ = status;
    return result;
  }

  E2ECheck Fail(E2ECheck result, E2EStatus status) noexcept {
    result.ok = false;
    result.status = status;
    status_ = status;
    return result;
  }

  E2ECheck Analyse(std::uint8_t delta, std::uint8_t counter, E2ECheck& result) noexcept {
    if (0U == delta) {
      if (no_new_or_repeated_ < 15U) {
        ++no_new_or_repeated_;
      }
      return Deliver(result, E2EStatus::kRepeated);
    }
    const bool in_window = (delta <= max_delta_counter_);
    if (in_window) {
      max_delta_counter_ = MaxDeltaCounterInit;
      last_valid_counter_ = counter;
      lost_data_ = static_cast<std::uint8_t>(delta - 1U);
      if (no_new_or_repeated_ <= MaxNoNewOrRepeatedData) {
        no_new_or_repeated_ = 0U;
        if (sync_counter_ > 0U) {
          --sync_counter_;
          return Fail(result, E2EStatus::kSync);
        }
        return Deliver(result, (1U == delta) ? E2EStatus::kOk : E2EStatus::kOkSomeLost);
      }
      no_new_or_repeated_ = 0U;
      sync_counter_ = SyncCounterInit;
      return Fail(result, E2EStatus::kSync);
    }
    no_new_or_repeated_ = 0U;
    sync_counter_ = SyncCounterInit;
    if (sync_counter_ > 0U) {
      max_delta_counter_ = MaxDeltaCounterInit;
      last_valid_counter_ = counter;
      return Fail(result, E2EStatus::kWrongSequence);
    }
    return Fail(result, E2EStatus::kWrongSequence);
  }

  static std::uint8_t ComputeCrc(osp::span<const std::uint8_t> payload,
                                 std::uint8_t counter) noexcept {
    Crc8H2f crc;
    crc.Update(osp::span<const std::uint8_t>(payload.data() + 1U, payload.size() - 1U));
    const std::uint8_t data_id = Ids::kDataIds[counter & 0x0FU];
    crc.Update(osp::span<const std::uint8_t>(&data_id, 1U));
    return crc.Value();
  }

  std::uint8_t counter_{0U};
  std::uint8_t last_valid_counter_{0U};
  std::uint8_t max_delta_counter_{MaxDeltaCounterInit};
  std::uint8_t no_new_or_repeated_{0U};
  std::uint8_t sync_counter_{0U};
  std::uint8_t lost_data_{0U};
  bool wait_for_first_{true};
  E2EStatus status_{E2EStatus::kNotChecked};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_E2E_PROFILE2_HPP_
