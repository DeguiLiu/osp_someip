/**
 * @file test_message_type.cpp
 * @brief Unit tests for the message type field and its exhaustive decode.
 */

#include "osp/someip/message_type.hpp"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using osp::someip::ErrorCode;
using osp::someip::MessageType;
using osp::someip::MessageTypeField;

TEST_CASE("message type - wire byte for every variant", "[message_type]") {
  const struct {
    MessageType type;
    std::uint8_t wire;
  } cases[] = {
      {MessageType::kRequest, 0x00U},   {MessageType::kRequestNoReturn, 0x01U},
      {MessageType::kNotification, 0x02U}, {MessageType::kResponse, 0x80U},
      {MessageType::kError, 0x81U},
  };
  for (const auto& item : cases) {
    const MessageTypeField plain = MessageTypeField::New(item.type, false);
    REQUIRE(item.wire == plain.Value());
    REQUIRE(item.type == plain.Type());
    REQUIRE_FALSE(plain.IsTp());

    const MessageTypeField tp = MessageTypeField::New(item.type, true);
    REQUIRE(static_cast<std::uint8_t>(item.wire | 0x20U) == tp.Value());
    REQUIRE(item.type == tp.Type());
    REQUIRE(tp.IsTp());
  }
}

TEST_CASE("message type - SD field is a plain notification", "[message_type]") {
  const MessageTypeField sd = MessageTypeField::NewSd();
  REQUIRE(MessageType::kNotification == sd.Type());
  REQUIRE_FALSE(sd.IsTp());
  REQUIRE(0x02U == sd.Value());
}

TEST_CASE("message type - only ten of 256 bytes decode", "[message_type]") {
  const std::uint8_t valid[] = {0x00U, 0x01U, 0x02U, 0x80U, 0x81U,
                                0x20U, 0x21U, 0x22U, 0xA0U, 0xA1U};
  for (std::uint32_t raw = 0U; raw <= 0xFFU; ++raw) {
    const auto decoded = MessageTypeField::FromByte(static_cast<std::uint8_t>(raw));
    bool expected_valid = false;
    for (const std::uint8_t candidate : valid) {
      if (candidate == raw) {
        expected_valid = true;
      }
    }
    REQUIRE(expected_valid == decoded.has_value());
    if (decoded.has_value()) {
      REQUIRE(static_cast<std::uint8_t>(raw & 0xDFU) ==
              static_cast<std::uint8_t>(decoded.value().Type()));
    }
  }
  REQUIRE(ErrorCode::kInvalidMessageTypeField ==
          MessageTypeField::FromByte(0x03U).get_error().code);
}
