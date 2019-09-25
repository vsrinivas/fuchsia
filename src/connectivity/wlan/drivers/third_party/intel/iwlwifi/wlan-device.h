// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The place holder for the code to interact with the MLME.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_WLAN_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_WLAN_DEVICE_H_

#include <ddk/device.h>
#include <ddk/protocol/wlanphyimpl.h>

#include "garnet/lib/wlan/protocol/include/wlan/protocol/mac.h"

extern wlanmac_protocol_ops_t wlanmac_ops;
extern zx_protocol_device_t device_mac_ops;  // for testing only
extern wlanphy_impl_protocol_ops_t wlanphy_ops;

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_WLAN_DEVICE_H_
