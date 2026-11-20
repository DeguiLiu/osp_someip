/**
 * @file test_message_id.cpp
 * @brief Unit tests for the service/method split of a SOME/IP message ID.
 */

#include "osp/someip/message_id.hpp"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using osp::someip::MessageId;

TEST_CASE("message id - service and method split", "[message_id]") {
  const MessageId id = MessageId::FromServiceAndMethod(0x1234U, 0x5678U);
  REQUIRE(0x12345678U == id.Value());
  REQUIRE(0x1234U == id.ServiceId());
  REQUIRE(0x5678U == id.MethodId());
}

TEST_CASE("message id - explicit value round trip", "[message_id]") {
  const MessageId id(0xABCDEF01U);
  REQUIRE(0xABCDEF01U == id.Value());
  REQUIRE(0xABCDU == id.ServiceId());
  REQUIRE(0xEF01U == id.MethodId());
}

TEST_CASE("message id - setters preserve the other half", "[message_id]") {
  MessageId id = MessageId::FromServiceAndMethod(0x0001U, 0x00FFU);
  id.SetServiceId(0xAAAAU);
  REQUIRE(0xAAAAU == id.ServiceId());
  REQUIRE(0x00FFU == id.MethodId());

  id = MessageId::FromServiceAndMethod(0x00FFU, 0x0001U);
  id.SetMethodId(0xBBBBU);
  REQUIRE(0x00FFU == id.ServiceId());
  REQUIRE(0xBBBBU == id.MethodId());

  id.SetValue(0xDEADBEEFU);
  REQUIRE(0xDEADBEEFU == id.Value());
}

TEST_CASE("message id - event flag follows the method high bit", "[message_id]") {
  REQUIRE(MessageId::FromServiceAndMethod(0x0001U, 0x8001U).IsEvent());
  REQUIRE_FALSE(MessageId::FromServiceAndMethod(0x0001U, 0x0001U).IsEvent());
}

TEST_CASE("message id - service discovery constant", "[message_id]") {
  const MessageId sd = MessageId::Sd();
  REQUIRE(0xFFFF8100U == sd.Value());
  REQUIRE(0xFFFFU == sd.ServiceId());
  REQUIRE(0x8100U == sd.MethodId());
  REQUIRE(sd.IsSd());
  REQUIRE_FALSE(MessageId(0x00010001U).IsSd());
}
