// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

namespace network {
namespace tun {

bool TryConsolidateBaseConfig(fuchsia::net::tun::BaseConfig* config) {
  // Check mandatory fields.
  if (!(config->has_rx_types() && config->has_tx_types())) {
    return false;
  }
  // Fill up default fields.
  if (!config->has_mtu()) {
    config->set_mtu(fuchsia::net::tun::MAX_MTU);
  }
  if (!config->has_report_metadata()) {
    config->set_report_metadata(false);
  }
  if (!config->has_min_tx_buffer_length()) {
    config->set_min_tx_buffer_length(0);
  }

  // Check field validity.
  return !(config->tx_types().empty() || config->rx_types().empty() || config->mtu() == 0 ||
           config->mtu() > fuchsia::net::tun::MAX_MTU);
}

bool TryConsolidateDeviceConfig(fuchsia::net::tun::DeviceConfig* config) {
  // Check mandatory fields.
  if (!config->has_base()) {
    return false;
  }
  // Fill up default fields.
  if (!config->has_online()) {
    config->set_online(false);
  }
  if (!config->has_blocking()) {
    config->set_blocking(false);
  }
  return TryConsolidateBaseConfig(config->mutable_base());
}

bool TryConsolidateDevicePairConfig(fuchsia::net::tun::DevicePairConfig* config) {
  // Check mandatory fields.
  if (!config->has_base()) {
    return false;
  }
  // Fill up default fields.
  if (!config->has_fallible_transmit_left()) {
    config->set_fallible_transmit_left(false);
  }
  if (!config->has_fallible_transmit_right()) {
    config->set_fallible_transmit_right(false);
  }
  return TryConsolidateBaseConfig(config->mutable_base());
}

}  // namespace tun
}  // namespace network
