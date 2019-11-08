// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/wlan/info.h>
#include <zircon/compiler.h>

extern "C" {

__EXPORT extern const wlan_channel_t test_wlan_channel = {
    .primary = 1,
    .cbw = WLAN_CHANNEL_BANDWIDTH__80P80,
    .secondary80 = 3,
};

__EXPORT extern const wlan_bss_config_t test_wlan_bss_config = {
    .bssid = {1, 2, 3, 4, 5, 6},
    .bss_type = WLAN_BSS_TYPE_PERSONAL,
    .remote = true,
};

}
