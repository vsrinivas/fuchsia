// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_STATS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_STATS_H_

// This file is used by the driver C code to report and dump the current statistics.

#include <lib/async/dispatcher.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/banjo/softmac.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/debug.h"

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

enum iwl_stats_counter_index {
  IWL_STATS_CNT_INTS_FROM_FW = 0,  // Interrupts from the firmware
  IWL_STATS_CNT_CMD_FROM_FW,       // Commands from the firmware
  IWL_STATS_CNT_BEACON_TO_MLME,    // Beacon packet to the MLME
  IWL_STATS_CNT_BCAST_TO_MLME,     // Broadcast packet to the MLME
  IWL_STATS_CNT_MCAST_TO_MLME,     // Multicast packet to the MLME
  IWL_STATS_CNT_UNICAST_TO_MLME,   // Unicast packet to the MLME
  IWL_STATS_CNT_DATA_FROM_MLME,    // Data from the MLME
  IWL_STATS_CNT_DATA_TO_FW,        // Data sent to the firmware
  IWL_STATS_CNT_CMD_TO_FW,         // Host commands sent to the firmware
  IWL_STATS_CNT_MAX,               // Always at the end of list.
};

// Initialize the feature.
void iwl_stats_init(async_dispatcher_t* dispatcher);

// Start the periodical task to print the statistics data to the log.
void iwl_stats_start_reporting(void);

// Update the latest WiFi signal status.
void iwl_stats_update_last_rssi(int8_t rssi_dbm);
void iwl_stats_update_date_rate(uint32_t data_rate);

// For testing.
size_t iwl_stats_read(enum iwl_stats_counter_index index);

// Increase one for a counter.
void iwl_stats_inc(enum iwl_stats_counter_index index);

// Analyze the WiFi packet and increase the corresponding counter.
void iwl_stats_analyze_rx(const wlan_rx_packet_t* pkt);

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_STATS_H_
