// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

namespace network {
namespace tun {

std::optional<BasePortConfig> BasePortConfig::Create(
    const fuchsia_net_tun::wire::BasePortConfig& config) {
  BasePortConfig out{
      .mtu = config.has_mtu() ? config.mtu() : fuchsia_net_tun::wire::kMaxMtu,
  };
  // Check validity.
  if (out.mtu == 0 || out.mtu > fuchsia_net_tun::wire::kMaxMtu) {
    return std::nullopt;
  }
  // Check required fields.
  if (!config.has_id() || config.id() >= fuchsia_hardware_network::wire::kMaxPorts) {
    return std::nullopt;
  }
  out.port_id = config.id();

  if (!config.has_rx_types()) {
    return std::nullopt;
  }
  auto const& rx_types = config.rx_types();
  if (rx_types.empty()) {
    return std::nullopt;
  }
  std::copy(rx_types.begin(), rx_types.end(), std::back_inserter(out.rx_types));
  if (!config.has_tx_types()) {
    return std::nullopt;
  }
  auto const& tx_types = config.tx_types();
  if (tx_types.empty()) {
    return std::nullopt;
  }
  std::copy(tx_types.begin(), tx_types.end(), std::back_inserter(out.tx_types));
  return out;
}

std::optional<DevicePortConfig> DevicePortConfig::Create(
    const fuchsia_net_tun::wire::DevicePortConfig& config) {
  if (!config.has_base()) {
    return std::nullopt;
  }
  std::optional base = BasePortConfig::Create(config.base());
  if (!base.has_value()) {
    return std::nullopt;
  }

  DevicePortConfig out(std::move(base.value()));
  out.online = config.has_online() && config.online();
  if (config.has_mac()) {
    out.mac = config.mac();
  }
  return out;
}

std::optional<DevicePairPortConfig> DevicePairPortConfig::Create(
    const fuchsia_net_tun::wire::DevicePairPortConfig& config) {
  if (!config.has_base()) {
    return std::nullopt;
  }
  std::optional base = BasePortConfig::Create(config.base());
  if (!base.has_value()) {
    return std::nullopt;
  }

  DevicePairPortConfig out(std::move(base.value()));
  if (config.has_mac_left()) {
    out.mac_left = config.mac_left();
  }
  if (config.has_mac_right()) {
    out.mac_right = config.mac_right();
  }
  return out;
}

BaseDeviceConfig::BaseDeviceConfig(const fuchsia_net_tun::wire::BaseDeviceConfig& config) {
  if (config.has_report_metadata()) {
    report_metadata = config.report_metadata();
  }
  if (config.has_min_tx_buffer_length()) {
    min_tx_buffer_length = config.min_tx_buffer_length();
  }
  if (config.has_min_rx_buffer_length()) {
    min_rx_buffer_length = config.min_rx_buffer_length();
  }
}

DeviceConfig::DeviceConfig(const fuchsia_net_tun::wire::DeviceConfig2& config)
    : BaseDeviceConfig([&config]() {
        if (config.has_base()) {
          return BaseDeviceConfig(config.base());
        } else {
          return BaseDeviceConfig(fuchsia_net_tun::wire::BaseDeviceConfig());
        }
      }()) {
  if (config.has_blocking()) {
    blocking = config.blocking();
  }
}

DevicePairConfig::DevicePairConfig(const fuchsia_net_tun::wire::DevicePairConfig& config)
    : BaseDeviceConfig([&config]() {
        if (config.has_base()) {
          return BaseDeviceConfig(config.base());
        } else {
          return BaseDeviceConfig(fuchsia_net_tun::wire::BaseDeviceConfig());
        }
      }()) {
  if (config.has_fallible_transmit_left()) {
    fallible_transmit_left = config.fallible_transmit_left();
  }
  if (config.has_fallible_transmit_right()) {
    fallible_transmit_right = config.fallible_transmit_right();
  }
}

}  // namespace tun
}  // namespace network
