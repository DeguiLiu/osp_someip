/**
 * @file header.hpp
 * @brief Encodable SOME/IP-SD payload header.
 *
 * Wire layout of the SD payload:
 * @code
 *   flags(1) | reserved(3) | entries_size(4) | entries | options_size(4) | options
 * @endcode
 * The header only concatenates the entries and options it was handed; indices
 * inside the entries are owned by the caller and are never rewritten here.
 * Encoding is bounds-checked and allocation-free.
 */

#ifndef OSP_SOMEIP_SD_HEADER_HPP_
#define OSP_SOMEIP_SD_HEADER_HPP_

#include "osp/someip/error.hpp"
#include "osp/someip/sd/entry.hpp"
#include "osp/someip/sd/entry_view.hpp"
#include "osp/someip/sd/option_view.hpp"
#include "osp/someip/sd/options.hpp"
#include "osp/someip/sd/types.hpp"
#include "osp/someip/wire.hpp"
#include "osp/span.hpp"
#include "osp/vocabulary.hpp"

#include <cstddef>
#include <cstdint>

namespace osp::someip::sd {

/// @brief Fixed part of the SD payload: flags, reserved, and the two size fields.
inline constexpr std::uint32_t kSdHeaderFixedSize = 12U;

/// @brief Offset of the entries section: flags(4) + entries_size(4).
inline constexpr std::size_t kEntriesOffset = 8U;

/**
 * @brief Borrowed SD header over caller-owned entry and option slices.
 *
 * The slices must outlive the header. The entries and options are encoded in
 * the order given; their `options_count` fields and option run indices are
 * expected to be preset by the caller.
 */
class Header final : public osp::someip::WireEncoder<Header> {
 public:
  Header() noexcept = default;

  Header(Flags flags, osp::span<const Entry> entries, osp::span<const Options> options) noexcept
      : flags_(flags), entries_(entries), options_(options) {}

  /// @brief SD flags byte as decoded from the two high bits.
  Flags GetFlags() const noexcept { return flags_; }

  /// @brief Borrowed entries, in wire order.
  osp::span<const Entry> Entries() const noexcept { return entries_; }

  /// @brief Borrowed options, in wire order.
  osp::span<const Options> OptionsList() const noexcept { return options_; }

  /// @brief Wire size: fixed part + 16 bytes per entry + each option's size.
  std::uint32_t DoEncodedSize() const noexcept {
    return static_cast<std::uint32_t>(kSdHeaderFixedSize + EntriesSize() + OptionsSize());
  }

  osp::expected<std::uint32_t, Error> DoEncodeTo(
      osp::span<std::uint8_t> out) const noexcept {
    const std::size_t needed =
        static_cast<std::size_t>(kSdHeaderFixedSize) + EntriesSize() + OptionsSize();
    if (needed > out.size()) {
      return osp::expected<std::uint32_t, Error>::error(
          Error::InsufficientBuffer(static_cast<std::uint32_t>(needed), out.size_u32()));
    }
    ByteWriter writer(out);
    const bool fixed_ok =
        writer.WriteU8(flags_.Value()) && writer.WriteU8(0U) && writer.WriteU8(0U) &&
        writer.WriteU8(0U) &&
        writer.WriteU32Be(static_cast<std::uint32_t>(EntriesSize()));
    if (!fixed_ok) {
      return osp::expected<std::uint32_t, Error>::error(
          Error::InsufficientBuffer(static_cast<std::uint32_t>(needed), out.size_u32()));
    }
    const auto entries_done = EncodeEntries(out, writer.Position());
    if (!entries_done.has_value()) {
      return osp::expected<std::uint32_t, Error>::error(entries_done.get_error());
    }
    const auto options_done = EncodeOptions(out, entries_done.value());
    if (!options_done.has_value()) {
      return osp::expected<std::uint32_t, Error>::error(options_done.get_error());
    }
    return osp::expected<std::uint32_t, Error>::success(
        static_cast<std::uint32_t>(options_done.value()));
  }

 private:
  std::size_t EntriesSize() const noexcept {
    return entries_.size() * static_cast<std::size_t>(kEntrySize);
  }

  std::size_t OptionsSize() const noexcept {
    std::size_t total = 0U;
    for (std::size_t i = 0U; i < options_.size(); ++i) {
      total += options_[i].EncodedSize();
    }
    return total;
  }

  /// @brief Encode the entries section and return the offset just past it.
  osp::expected<std::size_t, Error> EncodeEntries(osp::span<std::uint8_t> out,
                                                  std::size_t offset) const noexcept {
    for (std::size_t i = 0U; i < entries_.size(); ++i) {
      const auto written =
          entries_[i].EncodeTo(osp::make_span(out.data() + offset, out.size() - offset));
      if (!written.has_value()) {
        return osp::expected<std::size_t, Error>::error(written.get_error());
      }
      offset += written.value();
    }
    return osp::expected<std::size_t, Error>::success(offset);
  }

  /// @brief Write options_size, then encode the options section.
  osp::expected<std::size_t, Error> EncodeOptions(osp::span<std::uint8_t> out,
                                                  std::size_t offset) const noexcept {
    const std::size_t options_size = OptionsSize();
    ByteWriter writer(osp::make_span(out.data() + offset, out.size() - offset));
    if (!writer.WriteU32Be(static_cast<std::uint32_t>(options_size))) {
      return osp::expected<std::size_t, Error>::error(
          Error::InsufficientBuffer(static_cast<std::uint32_t>(offset + 4U + options_size),
                                    out.size_u32()));
    }
    offset += writer.Position();
    for (std::size_t i = 0U; i < options_.size(); ++i) {
      const auto written =
          options_[i].EncodeTo(osp::make_span(out.data() + offset, out.size() - offset));
      if (!written.has_value()) {
        return osp::expected<std::size_t, Error>::error(written.get_error());
      }
      offset += written.value();
    }
    return osp::expected<std::size_t, Error>::success(offset);
  }

  Flags flags_{};
  osp::span<const Entry> entries_{};
  osp::span<const Options> options_{};
};

/**
 * @brief Zero-copy, pre-validated view over an SD payload.
 *
 * Parse() checks the section bounds, that entries_size is a multiple of 16,
 * and validates every option (length, per-type length, transport-protocol
 * byte). Unknown option types are kept in place. Unknown entry types are
 * skipped by NextEntry(), matching the reference implementation.
 */
class SdHeaderView final {
 public:
  constexpr SdHeaderView() noexcept = default;

  static osp::expected<SdHeaderView, Error> Parse(
      osp::span<const std::uint8_t> payload) noexcept {
    if (payload.size() < kSdHeaderFixedSize) {
      return osp::expected<SdHeaderView, Error>::error(
          Error::Incomplete(kSdHeaderFixedSize, static_cast<std::uint32_t>(payload.size())));
    }
    const std::uint32_t entries_size = detail::ReadU32Be(payload, 4U);
    if (0U != (entries_size % kEntrySize)) {
      return osp::expected<SdHeaderView, Error>::error(Error::InvalidLength(entries_size));
    }
    const std::size_t options_size_offset = 8U + entries_size;
    if (payload.size() < options_size_offset + 4U) {
      return osp::expected<SdHeaderView, Error>::error(Error::Incomplete(
          static_cast<std::uint32_t>(options_size_offset + 4U),
          static_cast<std::uint32_t>(payload.size())));
    }
    const std::uint32_t options_size = detail::ReadU32Be(payload, options_size_offset);
    const std::size_t total =
        options_size_offset + 4U + static_cast<std::size_t>(options_size);
    if (payload.size() < total) {
      return osp::expected<SdHeaderView, Error>::error(Error::Incomplete(
          static_cast<std::uint32_t>(total), static_cast<std::uint32_t>(payload.size())));
    }
    SdHeaderView view;
    view.flags_ = Flags::FromByte(payload[0U]);
    view.entries_ = osp::span<const std::uint8_t>(payload.data() + kEntriesOffset,
                                                  entries_size);
    view.options_bytes_ = osp::span<const std::uint8_t>(
        payload.data() + options_size_offset + 4U, options_size);
    view.options_ = OptionWalker(view.options_bytes_);
    Error err = view.ValidateOptions();
    if (ErrorCode::kNone != err.code) {
      return osp::expected<SdHeaderView, Error>::error(err);
    }
    return osp::expected<SdHeaderView, Error>::success(view);
  }

  constexpr Flags GetFlags() const noexcept { return flags_; }
  constexpr osp::span<const std::uint8_t> EntriesBytes() const noexcept { return entries_; }
  constexpr osp::span<const std::uint8_t> OptionsBytes() const noexcept {
    return options_bytes_;
  }

  /// @brief Next entry of a known type; unknown types are skipped.
  bool NextEntry(EntryView& out) noexcept {
    while (entry_offset_ + kEntrySize <= entries_.size()) {
      const auto slice = osp::span<const std::uint8_t>(entries_.data() + entry_offset_,
                                                       kEntrySize);
      entry_offset_ += kEntrySize;
      auto parsed = EntryView::Parse(slice);
      if (parsed.has_value()) {
        out = parsed.value();
        return true;
      }
    }
    return false;
  }

  /// @brief Next option; the section was validated by Parse().
  bool NextOption(OptionView& out) noexcept { return options_.Next(out); }

 private:
  Error ValidateOptions() noexcept {
    Error err = Error::None();
    osp::span<const std::uint8_t> rest = options_bytes_;
    while (!rest.empty() && ErrorCode::kNone == err.code) {
      auto parsed = OptionView::Parse(rest);
      if (!parsed.has_value()) {
        err = parsed.get_error();
      } else {
        rest = parsed.value().rest;
      }
    }
    if (ErrorCode::kNone == err.code && !rest.empty()) {
      err = Error::IncorrectOptionsSize(static_cast<std::uint32_t>(options_bytes_.size()),
                                        static_cast<std::uint32_t>(rest.size()));
    }
    return err;
  }

  Flags flags_{};
  osp::span<const std::uint8_t> entries_{};
  std::size_t entry_offset_{0U};
  osp::span<const std::uint8_t> options_bytes_{};
  OptionWalker options_{};
};

}  // namespace osp::someip::sd

#endif  // OSP_SOMEIP_SD_HEADER_HPP_
