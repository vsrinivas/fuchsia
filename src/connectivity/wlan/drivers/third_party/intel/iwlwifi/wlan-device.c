// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The place holder for the code to interact with the MLME.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"

// PHY interface
wlanphy_impl_protocol_ops_t wlanphy_ops = {
    .query = NULL,
    .create_iface = NULL,
    .destroy_iface = NULL,
    .set_country = NULL,
};
