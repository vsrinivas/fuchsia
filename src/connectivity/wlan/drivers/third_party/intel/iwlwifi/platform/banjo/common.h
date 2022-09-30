// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains copies of banjo definitions that were auto generated from fuchsia.wlan.common.
// Since banjo is being deprecated, we are making a local copy of defines that the driver relies
// upon. fxbug.dev/104598 is the tracking bug to remove the usage of platforms/banjo/*.h files.

// WARNING: DO NOT ADD MORE DEFINITIONS TO THIS FILE

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_COMMON_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_COMMON_H_

#include <zircon/types.h>

typedef uint32_t wlan_phy_type_t;
#define WLAN_PHY_TYPE_DSSS UINT32_C(1)
#define WLAN_PHY_TYPE_HR UINT32_C(2)
#define WLAN_PHY_TYPE_OFDM UINT32_C(3)
#define WLAN_PHY_TYPE_ERP UINT32_C(4)
#define WLAN_PHY_TYPE_HT UINT32_C(5)
#define WLAN_PHY_TYPE_DMG UINT32_C(6)
#define WLAN_PHY_TYPE_VHT UINT32_C(7)
#define WLAN_PHY_TYPE_TVHT UINT32_C(8)
#define WLAN_PHY_TYPE_S1G UINT32_C(9)
#define WLAN_PHY_TYPE_CDMG UINT32_C(10)
#define WLAN_PHY_TYPE_CMMG UINT32_C(11)
#define WLAN_PHY_TYPE_HE UINT32_C(12)
typedef uint32_t wlan_mac_role_t;
#define WLAN_MAC_ROLE_CLIENT UINT32_C(1)
#define WLAN_MAC_ROLE_AP UINT32_C(2)
#define WLAN_MAC_ROLE_MESH UINT32_C(3)
typedef uint8_t wlan_band_t;
#define WLAN_BAND_TWO_GHZ UINT8_C(0)
#define WLAN_BAND_FIVE_GHZ UINT8_C(1)
#define fuchsia_wlan_common_WLAN_TX_VECTOR_IDX_INVALID UINT16_C(0)
#define fuchsia_wlan_common_WLAN_TX_STATUS_MAX_ENTRY UINT32_C(8)
#define fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES UINT8_C(16)
// This constant defined the fixed length for arrays containing the capabilities
// for each band supported by a device driver.
#define fuchsia_wlan_common_MAX_BANDS UINT8_C(16)
typedef uint32_t channel_bandwidth_t;
#define CHANNEL_BANDWIDTH_CBW20 UINT32_C(0)
#define CHANNEL_BANDWIDTH_CBW40 UINT32_C(1)
#define CHANNEL_BANDWIDTH_CBW40BELOW UINT32_C(2)
#define CHANNEL_BANDWIDTH_CBW80 UINT32_C(3)
#define CHANNEL_BANDWIDTH_CBW160 UINT32_C(4)
#define CHANNEL_BANDWIDTH_CBW80P80 UINT32_C(5)
typedef struct wlan_channel wlan_channel_t;
typedef struct wlan_tx_status_entry wlan_tx_status_entry_t;
typedef struct wlan_tx_status wlan_tx_status_t;
typedef uint8_t wlan_tx_result_t;

struct wlan_channel {
  uint8_t primary;
  channel_bandwidth_t cbw;
  uint8_t secondary80;
};

// Declarations
// One entry in a WlanTxStatus report. Indicates a number of attempted transmissions on
// a particular tx vector, but does not imply successful transmission.
struct wlan_tx_status_entry {
  uint16_t tx_vector_idx;
  // Number of total attempts with this specific tx vector, including successful attempts.
  // DDK assumes the number of attempts per packet will not exceed 255. (typically <= 8)
  uint8_t attempts;
};

// TX status reports are used by the Minstrel rate selection algorithm
// Tests should use the default value in //src/connectivity/wlan/testing/hw-sim/src/lib.rs
struct wlan_tx_status {
  // up to 8 different tx_vector for one PPDU frame.
  // WLAN_TX_VECTOR_IDX_INVALID indicates no more entries.
  wlan_tx_status_entry_t tx_status_entry[8];
  // Destination mac address, or addr1 in packet header.
  uint8_t peer_addr[6];
  wlan_tx_result_t result;
};

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_COMMON_H_
