// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_LIB_NXP_INCLUDE_WIFI_WIFI_CONFIG_H_
#define SRC_DEVICES_LIB_NXP_INCLUDE_WIFI_WIFI_CONFIG_H_

#include <stdint.h>

namespace wlan::nxpfmac {

// NXP sdio wifi config
struct NxpSdioWifiConfig {
  bool client_support;
  bool softap_support;
  bool sdio_rx_aggr_enable;
  bool fixed_beacon_buffer;
  bool auto_ds;
  bool ps_mode;
  uint32_t max_tx_buf;
  bool cfg_11d;
  bool inact_tmo = false;
  uint32_t hs_wake_interval;
  uint32_t indication_gpio;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_DEVICES_LIB_NXP_INCLUDE_WIFI_WIFI_CONFIG_H_
