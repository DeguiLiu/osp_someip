/**
 * @file session.hpp
 * @brief SD session numbering: IDs count 1..0xFFFF and wrap back to 1.
 */

#ifndef OSP_SOMEIP_SD_SESSION_HPP_
#define OSP_SOMEIP_SD_SESSION_HPP_

#include "osp/someip/sd/types.hpp"

#include <atomic>
#include <cstdint>

namespace osp::someip::sd {

/// @brief Session ID and reboot flag of one SD message.
struct SdSession final {
  std::uint16_t id{1U};
  RebootFlag reboot{RebootFlag::kRecentlyRebooted};
};

/**
 * @brief Numbers SD messages.
 *
 * The reboot flag stays RecentlyRebooted until the counter wraps from 0xFFFF
 * to 1, then stays Continuous. Concurrent callers each get a distinct ID.
 * The counter is move-only: it owns an atomic and is meant to live in one
 * owner (a node or a connection), not to be copied.
 */
class SdSessionCounter final {
 public:
  /// @brief A counter whose first session is ID 1, recently rebooted.
  static SdSessionCounter New() noexcept { return SdSessionCounter(kNoSession); }

  /// @brief Resume after @p last with the given reboot state; 0 means none yet.
  static SdSessionCounter Resume(std::uint16_t last, RebootFlag reboot) noexcept {
    const std::uint32_t wrapped =
        (RebootFlag::kContinuous == reboot) ? kWrappedBit : 0U;
    return SdSessionCounter(static_cast<std::uint32_t>(last) | wrapped);
  }

  SdSessionCounter(SdSessionCounter&&) = delete;
  SdSessionCounter& operator=(SdSessionCounter&&) = delete;
  SdSessionCounter(const SdSessionCounter&) = delete;
  SdSessionCounter& operator=(const SdSessionCounter&) = delete;

  /// @brief The next session; wraps 0xFFFF to 1 and latches Continuous.
  SdSession Next() noexcept {
    std::uint32_t previous = state_.load(std::memory_order_acquire);
    std::uint32_t next = After(previous);
    while (!state_.compare_exchange_weak(previous, next, std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
      next = After(previous);
    }
    return SdSession{static_cast<std::uint16_t>(next & kIdMask),
                     (0U != (next & kWrappedBit)) ? RebootFlag::kContinuous
                                                  : RebootFlag::kRecentlyRebooted};
  }

 private:
  explicit SdSessionCounter(std::uint32_t state) noexcept : state_(state) {}

  static constexpr std::uint32_t After(std::uint32_t state) noexcept {
    const std::uint32_t last = state & kIdMask;
    const bool wrapped = (0U != (state & kWrappedBit)) || (kIdMask == last);
    const std::uint32_t id = (kIdMask == last) ? 1U : (last + 1U);
    return id | (wrapped ? kWrappedBit : 0U);
  }

  static constexpr std::uint32_t kIdMask = 0xFFFFU;
  static constexpr std::uint32_t kWrappedBit = 1U << 16U;
  static constexpr std::uint32_t kNoSession = 0U;

  std::atomic<std::uint32_t> state_;
};

}  // namespace osp::someip::sd

#endif  // OSP_SOMEIP_SD_SESSION_HPP_
