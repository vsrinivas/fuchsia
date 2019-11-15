// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include <ddk/hw/wlan/ieee80211.h>
#include <ddk/protocol/wlan/info.h>

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

__EXPORT extern const ieee80211_ht_capabilities_t test_ht_caps{
    .ht_capability_info = 1,
    .ampdu_params = 2,
    .supported_mcs_set =
        {
            .fields =
                ieee80211_ht_capabilities_supported_mcs_set_fields_t{
                    .rx_mcs_head = 3,
                    .rx_mcs_tail = 4,
                    .tx_mcs = 5,
                },
        },
    .ht_ext_capabilities = 6,
    .tx_beamforming_capabilities = 7,
    .asel_capabilities = 255,
};

__EXPORT extern const ieee80211_vht_capabilities_t test_vht_caps{
    .vht_capability_info = 1,
    .supported_vht_mcs_and_nss_set = static_cast<uint64_t>(1) << 63,
};
}
