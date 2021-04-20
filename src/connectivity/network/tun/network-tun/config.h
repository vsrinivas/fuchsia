// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_CONFIG_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_CONFIG_H_

#include <fuchsia/net/llcpp/fidl.h>
#include <fuchsia/net/tun/llcpp/fidl.h>

namespace network {
namespace tun {

class BaseConfig {
 public:
  BaseConfig() = delete;
  BaseConfig(BaseConfig&&) = default;

  static std::optional<BaseConfig> Create(const fuchsia_net_tun::wire::BaseConfig& config);

  uint32_t mtu;
  std::vector<fuchsia_hardware_network::wire::FrameType> rx_types;
  std::vector<fuchsia_hardware_network::wire::FrameTypeSupport> tx_types;
  bool report_metadata;
  uint32_t min_tx_buffer_length;
};

class DeviceConfig : public BaseConfig {
 public:
  DeviceConfig() = delete;
  DeviceConfig(DeviceConfig&&) = default;

  static std::optional<DeviceConfig> Create(const fuchsia_net_tun::wire::DeviceConfig& config);

  bool online;
  bool blocking;
  std::optional<fuchsia_net::wire::MacAddress> mac;

 private:
  explicit DeviceConfig(BaseConfig&& base) : BaseConfig(std::move(base)) {}
};

class DevicePairConfig : public BaseConfig {
 public:
  DevicePairConfig() = delete;
  DevicePairConfig(DevicePairConfig&&) = default;

  static std::optional<DevicePairConfig> Create(
      const fuchsia_net_tun::wire::DevicePairConfig& config);

  bool fallible_transmit_left;
  bool fallible_transmit_right;
  std::optional<fuchsia_net::wire::MacAddress> mac_left;
  std::optional<fuchsia_net::wire::MacAddress> mac_right;

 private:
  explicit DevicePairConfig(BaseConfig&& base) : BaseConfig(std::move(base)) {}
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_CONFIG_H_
