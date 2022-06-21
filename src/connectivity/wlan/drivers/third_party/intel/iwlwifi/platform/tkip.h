// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

// The library of TKIP algorithm.
//
// Currently we only implement the part that generates P1K (Phase 1 Key) since the driver only needs
// it (Phase 2 is not used by the driver).

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TKIP_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TKIP_H_

#include <stdint.h>
#include <stdlib.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define TKIP_P1K_SIZE 5  // in 16-bit unit. total 80-bit.
#define RC4_KEY_SIZE 16  // in 8-bit. total 128-bit.

// Use the same function name so that it is easier for the uprev in the future.
void ieee80211_get_tkip_rx_p1k(const struct ieee80211_key_conf* key_conf,
                               const uint8_t addr[ETH_ALEN], uint16_t p1k[TKIP_P1K_SIZE]);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TKIP_H_
