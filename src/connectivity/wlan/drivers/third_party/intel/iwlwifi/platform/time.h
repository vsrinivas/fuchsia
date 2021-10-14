// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TIME_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TIME_H_

#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

// Return the "now" time on this device's task dispatcher.
zx_time_t iwl_time_now(struct device* dev);

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TIME_H_
