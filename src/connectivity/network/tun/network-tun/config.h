// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_CONFIG_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_CONFIG_H_

#include <fidl/fuchsia.net.tun/cpp/wire.h>
#include <fidl/fuchsia.net/cpp/wire.h>

namespace network {
namespace tun {

class BasePortConfig {
 public:
  BasePortConfig() = delete;
  BasePortConfig(BasePortConfig&&) = default;

  static std::optional<BasePortConfig> Create(const fuchsia_net_tun::wire::BasePortConfig& config);

  uint8_t port_id;
  uint32_t mtu;
  std::vector<fuchsia_hardware_network::wire::FrameType> rx_types;
  std::vector<fuchsia_hardware_network::wire::FrameTypeSupport> tx_types;
};

class DevicePortConfig : public BasePortConfig {
 public:
  DevicePortConfig() = delete;
  DevicePortConfig(DevicePortConfig&&) = default;

  static std::optional<DevicePortConfig> Create(
      const fuchsia_net_tun::wire::DevicePortConfig& config);

  bool online;
  std::optional<fuchsia_net::wire::MacAddress> mac;

 private:
  explicit DevicePortConfig(BasePortConfig&& base) : BasePortConfig(std::move(base)) {}
};

class DevicePairPortConfig : public BasePortConfig {
 public:
  DevicePairPortConfig() = delete;
  DevicePairPortConfig(DevicePairPortConfig&&) = default;

  static std::optional<DevicePairPortConfig> Create(
      const fuchsia_net_tun::wire::DevicePairPortConfig& config);

  std::optional<fuchsia_net::wire::MacAddress> mac_left;
  std::optional<fuchsia_net::wire::MacAddress> mac_right;

 private:
  explicit DevicePairPortConfig(BasePortConfig&& base) : BasePortConfig(std::move(base)) {}
};

class BaseDeviceConfig {
 public:
  explicit BaseDeviceConfig(const fuchsia_net_tun::wire::BaseDeviceConfig& config);

  BaseDeviceConfig(BaseDeviceConfig&&) = default;

  bool report_metadata = false;
  uint32_t min_tx_buffer_length = 0;
  uint32_t min_rx_buffer_length = 0;
};

class DeviceConfig : public BaseDeviceConfig {
 public:
  explicit DeviceConfig(const fuchsia_net_tun::wire::DeviceConfig& config);
  DeviceConfig(DeviceConfig&&) = default;

  bool blocking = false;
};

class DevicePairConfig : public BaseDeviceConfig {
 public:
  explicit DevicePairConfig(const fuchsia_net_tun::wire::DevicePairConfig& config);

  bool fallible_transmit_left = false;
  bool fallible_transmit_right = false;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_CONFIG_H_
