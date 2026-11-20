/**
 * @file wire.hpp
 * @brief Bounded big-endian writer and the CRTP encoder skeleton.
 *
 * Every wire type derives from WireEncoder<T> and implements `DoEncodedSize()`
 * and `DoEncodeTo()`. The base supplies the public `EncodedSize()` /
 * `EncodeTo()` pair, so there is exactly one bounds-checking path and no
 * virtual dispatch. Encoding never allocates and never throws; a short output
 * slice is reported as ErrorCode::kInsufficientBuffer.
 */

#ifndef OSP_SOMEIP_WIRE_HPP_
#define OSP_SOMEIP_WIRE_HPP_

#include "osp/someip/error.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <cstddef>
#include <cstdint>

namespace osp::someip {

/// @brief Big-endian writer over a caller-owned buffer.
class ByteWriter final {
 public:
  explicit ByteWriter(osp::span<std::uint8_t> out) noexcept : out_(out) {}

  bool WriteU8(std::uint8_t value) noexcept {
    if (1U > Remaining()) {
      return false;
    }
    out_[position_] = value;
    ++position_;
    return true;
  }

  bool WriteU16Be(std::uint16_t value) noexcept {
    if (2U > Remaining()) {
      return false;
    }
    out_[position_] = static_cast<std::uint8_t>(value >> 8U);
    out_[position_ + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
    position_ += 2U;
    return true;
  }

  bool WriteU24Be(std::uint32_t value) noexcept {
    if (3U > Remaining()) {
      return false;
    }
    out_[position_] = static_cast<std::uint8_t>(value >> 16U);
    out_[position_ + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    out_[position_ + 2U] = static_cast<std::uint8_t>(value & 0xFFU);
    position_ += 3U;
    return true;
  }

  bool WriteU32Be(std::uint32_t value) noexcept {
    if (4U > Remaining()) {
      return false;
    }
    out_[position_] = static_cast<std::uint8_t>(value >> 24U);
    out_[position_ + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    out_[position_ + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    out_[position_ + 3U] = static_cast<std::uint8_t>(value & 0xFFU);
    position_ += 4U;
    return true;
  }

  bool WriteBytes(osp::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() > Remaining()) {
      return false;
    }
    for (std::size_t i = 0U; i < bytes.size(); ++i) {
      out_[position_ + i] = bytes[i];
    }
    position_ += bytes.size();
    return true;
  }

  std::size_t Position() const noexcept { return position_; }

  osp::span<const std::uint8_t> Written() const noexcept {
    return osp::span<const std::uint8_t>(out_.data(), position_);
  }

 private:
  std::size_t Remaining() const noexcept { return out_.size() - position_; }

  osp::span<std::uint8_t> out_;
  std::size_t position_{0U};
};

namespace detail {

constexpr std::uint16_t ReadU16Be(osp::span<const std::uint8_t> in,
                                  std::size_t offset) noexcept {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(in[offset]) << 8U) |
                                    static_cast<std::uint16_t>(in[offset + 1U]));
}

constexpr std::uint32_t ReadU24Be(osp::span<const std::uint8_t> in,
                                  std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(in[offset]) << 16U) |
         (static_cast<std::uint32_t>(in[offset + 1U]) << 8U) |
         static_cast<std::uint32_t>(in[offset + 2U]);
}

constexpr std::uint32_t ReadU32Be(osp::span<const std::uint8_t> in,
                                  std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(in[offset]) << 24U) |
         (static_cast<std::uint32_t>(in[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(in[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(in[offset + 3U]);
}

}  // namespace detail

/**
 * @brief CRTP encoder skeleton.
 *
 * @tparam Derived supplies `DoEncodedSize()` and
 *                `DoEncodeTo(osp::span<std::uint8_t>)`.
 */
template <typename Derived>
class WireEncoder {
 public:
  std::uint32_t EncodedSize() const noexcept {
    return static_cast<const Derived*>(this)->DoEncodedSize();
  }

  osp::expected<std::uint32_t, Error> EncodeTo(osp::span<std::uint8_t> out) const noexcept {
    return static_cast<const Derived*>(this)->DoEncodeTo(out);
  }

 protected:
  WireEncoder() = default;
  WireEncoder(const WireEncoder&) = default;
  WireEncoder(WireEncoder&&) = default;
  WireEncoder& operator=(const WireEncoder&) = default;
  WireEncoder& operator=(WireEncoder&&) = default;
  ~WireEncoder() = default;
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_WIRE_HPP_
