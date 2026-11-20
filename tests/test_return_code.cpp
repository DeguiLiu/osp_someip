/**
 * @file test_return_code.cpp
 * @brief Unit tests for return code ranges and byte round trips.
 */

#include "osp/someip/return_code.hpp"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using osp::someip::ReturnCode;
using osp::someip::ReturnCodeKind;

TEST_CASE("return code - every byte round trips up to 0x5E", "[return_code]") {
  for (std::uint32_t raw = 0U; raw <= 0xFFU; ++raw) {
    const auto decoded = ReturnCode::FromByte(static_cast<std::uint8_t>(raw));
    if (raw < 0x5FU) {
      REQUIRE(decoded.has_value());
      REQUIRE(static_cast<std::uint8_t>(raw) == decoded.value().Value());
    } else {
      REQUIRE_FALSE(decoded.has_value());
    }
  }
}

TEST_CASE("return code - ranges map to the right kind", "[return_code]") {
  REQUIRE(ReturnCodeKind::kNamed == ReturnCode::Ok().Kind());
  REQUIRE(ReturnCodeKind::kNamed == ReturnCode::WrongMessageType().Kind());
  REQUIRE(ReturnCodeKind::kGeneric == ReturnCode::Generic(0x0BU).Kind());
  REQUIRE(ReturnCodeKind::kGeneric == ReturnCode::Generic(0x1FU).Kind());
  REQUIRE(ReturnCodeKind::kInterface == ReturnCode::Interface(0x20U).Kind());
  REQUIRE(ReturnCodeKind::kInterface == ReturnCode::Interface(0x5EU).Kind());

  for (std::uint32_t raw = 0x0BU; raw <= 0x1FU; ++raw) {
    const auto decoded = ReturnCode::FromByte(static_cast<std::uint8_t>(raw));
    REQUIRE(ReturnCodeKind::kGeneric == decoded.value().Kind());
  }
  for (std::uint32_t raw = 0x20U; raw <= 0x5EU; ++raw) {
    const auto decoded = ReturnCode::FromByte(static_cast<std::uint8_t>(raw));
    REQUIRE(ReturnCodeKind::kInterface == decoded.value().Kind());
  }
}

TEST_CASE("return code - named values match their wire bytes", "[return_code]") {
  REQUIRE(0x00U == ReturnCode::Ok().Value());
  REQUIRE(0x01U == ReturnCode::NotOk().Value());
  REQUIRE(0x02U == ReturnCode::UnknownService().Value());
  REQUIRE(0x03U == ReturnCode::UnknownMethod().Value());
  REQUIRE(0x04U == ReturnCode::NotReady().Value());
  REQUIRE(0x05U == ReturnCode::NotReachable().Value());
  REQUIRE(0x06U == ReturnCode::Timeout().Value());
  REQUIRE(0x07U == ReturnCode::WrongProtocolVersion().Value());
  REQUIRE(0x08U == ReturnCode::WrongInterfaceVersion().Value());
  REQUIRE(0x09U == ReturnCode::MalformedMessage().Value());
  REQUIRE(0x0AU == ReturnCode::WrongMessageType().Value());
}

TEST_CASE("return code - equality includes the range kind", "[return_code]") {
  REQUIRE(ReturnCode::Ok() == ReturnCode::Ok());
  REQUIRE(ReturnCode::Ok() != ReturnCode::Generic(0x00U));
  REQUIRE(ReturnCode::Generic(0x15U) == ReturnCode::Generic(0x15U));
}
