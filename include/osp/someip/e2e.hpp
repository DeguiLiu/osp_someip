/**
 * @file e2e.hpp
 * @brief End-to-end protection policy hook.
 *
 * The default policy checks nothing and protects nothing; a profile is a
 * caller-supplied type with the same two member functions, bound at compile
 * time so the hot path pays nothing when the hook is unused.
 */

#ifndef OSP_SOMEIP_E2E_HPP_
#define OSP_SOMEIP_E2E_HPP_

#include "osp/someip/message_id.hpp"
#include "osp/span.hpp"

#include <array>
#include <cstdint>

namespace osp::someip {

/// @brief Outcome of one E2E check.
enum class E2EStatus : std::uint8_t {
  kNotChecked = 0U,
  kOk,
  kError,
};

/// @brief What a check found: whether it applied, whether it passed, and the payload to use.
struct E2ECheck final {
  bool applies{false};
  bool ok{true};
  E2EStatus status{E2EStatus::kNotChecked};
  /// @brief Payload after any protection trailer is removed.
  osp::span<const std::uint8_t> payload{};
};

/**
 * @brief No protection: every check reports "not applicable".
 *
 * A real profile implements:
 * @code
 *   E2ECheck Check(MessageId id, const std::array<std::uint8_t, 8>& upper_header,
 *                  osp::span<const std::uint8_t> payload) const noexcept;
 *   osp::span<const std::uint8_t> Protect(MessageId id,
 *                                         const std::array<std::uint8_t, 8>& upper_header,
 *                                         osp::span<std::uint8_t> buffer,
 *                                         std::uint32_t payload_size) const noexcept;
 * @endcode
 * Protect() writes in place and must return a view that starts at buffer.data();
 * it may only change the length (for example by appending a CRC trailer).
 */
class NoE2E final {
 public:
  E2ECheck Check(MessageId /*id*/, const std::array<std::uint8_t, 8U>& /*upper_header*/,
                 osp::span<const std::uint8_t> payload) const noexcept {
    return E2ECheck{false, true, E2EStatus::kNotChecked, payload};
  }

  osp::span<const std::uint8_t> Protect(MessageId /*id*/,
                                        const std::array<std::uint8_t, 8U>& /*upper_header*/,
                                        osp::span<std::uint8_t> buffer,
                                        std::uint32_t payload_size) const noexcept {
    return osp::span<const std::uint8_t>(buffer.data(), payload_size);
  }
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_E2E_HPP_
