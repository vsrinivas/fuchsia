// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

namespace network {
namespace tun {

std::optional<BaseConfig> BaseConfig::Create(const fuchsia_net_tun::wire::BaseConfig& config) {
  BaseConfig out{
      .mtu = config.has_mtu() ? config.mtu() : fuchsia_net_tun::wire::kMaxMtu,
      .report_metadata = config.has_report_metadata() && config.report_metadata(),
      .min_tx_buffer_length = config.has_min_tx_buffer_length() ? config.min_tx_buffer_length() : 0,
  };
  // Check validity.
  if (out.mtu == 0 || out.mtu > fuchsia_net_tun::wire::kMaxMtu) {
    return std::nullopt;
  }
  // Check required fields.
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

std::optional<DeviceConfig> DeviceConfig::Create(
    const fuchsia_net_tun::wire::DeviceConfig& config) {
  // Check validity.
  if (!config.has_base()) {
    return std::nullopt;
  }
  std::optional base = BaseConfig::Create(config.base());
  if (!base.has_value()) {
    return std::nullopt;
  }
  DeviceConfig out(std::move(base.value()));
  out.online = config.has_online() && config.online();
  out.blocking = config.has_blocking() && config.blocking();
  if (config.has_mac()) {
    out.mac = config.mac();
  }
  return out;
}

std::optional<DevicePairConfig> DevicePairConfig::Create(
    const fuchsia_net_tun::wire::DevicePairConfig& config) {
  // Check validity.
  if (!config.has_base()) {
    return std::nullopt;
  }
  std::optional base = BaseConfig::Create(config.base());
  if (!base.has_value()) {
    return std::nullopt;
  }
  DevicePairConfig out(std::move(base.value()));
  out.fallible_transmit_left =
      config.has_fallible_transmit_left() && config.fallible_transmit_left();
  out.fallible_transmit_right =
      config.has_fallible_transmit_right() && config.fallible_transmit_right();
  if (config.has_mac_left()) {
    out.mac_left = config.mac_left();
  }
  if (config.has_mac_right()) {
    out.mac_right = config.mac_right();
  }
  return out;
}

}  // namespace tun
}  // namespace network
