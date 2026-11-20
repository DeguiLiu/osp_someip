/**
 * @file service_instance.hpp
 * @brief Service identity: service ID, instance ID, major version.
 */

#ifndef OSP_SOMEIP_SERVICE_INSTANCE_HPP_
#define OSP_SOMEIP_SERVICE_INSTANCE_HPP_

#include <cstdint>

namespace osp::someip {

/// @brief Identity of one service instance.
class ServiceInstance final {
 public:
  constexpr ServiceInstance() noexcept = default;

  static constexpr ServiceInstance New(std::uint16_t service_id, std::uint16_t instance_id,
                                       std::uint8_t major_version) noexcept {
    return ServiceInstance(service_id, instance_id, major_version);
  }

  constexpr std::uint16_t ServiceId() const noexcept { return service_id_; }
  constexpr std::uint16_t InstanceId() const noexcept { return instance_id_; }
  constexpr std::uint8_t MajorVersion() const noexcept { return major_version_; }

  /// @brief True when any field is the SD wildcard (0xFFFF / 0xFF).
  constexpr bool IsWildcard() const noexcept {
    return 0xFFFFU == service_id_ || 0xFFFFU == instance_id_ || 0xFFU == major_version_;
  }

  friend constexpr bool operator==(const ServiceInstance& lhs,
                                   const ServiceInstance& rhs) noexcept {
    return lhs.service_id_ == rhs.service_id_ && lhs.instance_id_ == rhs.instance_id_ &&
           lhs.major_version_ == rhs.major_version_;
  }
  friend constexpr bool operator!=(const ServiceInstance& lhs,
                                   const ServiceInstance& rhs) noexcept {
    return !(lhs == rhs);
  }

 private:
  constexpr ServiceInstance(std::uint16_t service_id, std::uint16_t instance_id,
                            std::uint8_t major_version) noexcept
      : service_id_(service_id), instance_id_(instance_id), major_version_(major_version) {}

  std::uint16_t service_id_{0U};
  std::uint16_t instance_id_{0U};
  std::uint8_t major_version_{0U};
};

}  // namespace osp::someip

#endif  // OSP_SOMEIP_SERVICE_INSTANCE_HPP_
