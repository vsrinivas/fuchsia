// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains copies of banjo definitions that were auto generated from
// fuchsia.hardware.wlan.associnfo. Since banjo is being deprecated, we are making a local copy of
// defines that the driver relies upon. fxbug.dev/104598 is the tracking bug to remove the usage
// of platforms/banjo/*.h files.

// WARNING: DO NOT ADD MORE DEFINITIONS TO THIS FILE

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_ASSOCINFO_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_ASSOCINFO_H_

#include <zircon/types.h>

typedef uint32_t wlan_rx_info_valid_t;
#define WLAN_RX_INFO_VALID_PHY UINT32_C(0x1)
#define WLAN_RX_INFO_VALID_DATA_RATE UINT32_C(0x2)
#define WLAN_RX_INFO_VALID_CHAN_WIDTH UINT32_C(0x4)
#define WLAN_RX_INFO_VALID_MCS UINT32_C(0x8)
#define WLAN_RX_INFO_VALID_RSSI UINT32_C(0x10)
#define WLAN_RX_INFO_VALID_SNR UINT32_C(0x20)
typedef uint8_t wlan_key_type_t;
#define WLAN_KEY_TYPE_PAIRWISE UINT8_C(1)
#define WLAN_KEY_TYPE_GROUP UINT8_C(2)
#define WLAN_KEY_TYPE_IGTK UINT8_C(3)
#define WLAN_KEY_TYPE_PEER UINT8_C(4)
#define WLAN_MAC_MAX_RATES UINT32_C(263)
#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_ASSOCINFO_H_
