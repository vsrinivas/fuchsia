// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_utils.h"

#include <wlan/common/channel.h>
#include <wlan/mlme/assoc_context.h>
#include <wlan/protocol/info.h>
#include <wlan/protocol/mac.h>

namespace wlan {
namespace test_utils {

AssocContext FakeAssocCtx() {
    wlan_ht_caps_t ht_cap_ddk{
        .ht_capability_info = 0x016e,
        .ampdu_params = 0x17,
        .mcs_set.rx_mcs_head = 0x00000001000000ff,
        .mcs_set.rx_mcs_tail = 0x01000000,
        .mcs_set.tx_mcs = 0x00000000,
        .ht_ext_capabilities = 0x1234,
        .tx_beamforming_capabilities = 0x12345678,
        .asel_capabilities = 0xff,
    };
    wlan_ht_op_t ht_op_ddk{
        .primary_chan = 123,
        .head = 0x01020304,
        .tail = 0x05,
        .basic_mcs_set.rx_mcs_head = 0x00000001000000ff,
        .basic_mcs_set.rx_mcs_tail = 0x01000000,
        .basic_mcs_set.tx_mcs = 0x00000000,
    };
    wlan_vht_caps_t vht_cap_ddk{
        .vht_capability_info = 0x0f805032,
        .supported_vht_mcs_and_nss_set = 0x0000fffe0000fffe,
    };

    wlan_vht_op_t vht_op_ddk{
        .vht_cbw = 0x01,
        .center_freq_seg0 = 42,
        .center_freq_seg1 = 106,
        .basic_mcs = 0x1122,
    };

    AssocContext ctx{};
    ctx.ht_cap = HtCapabilities::FromDdk(ht_cap_ddk);
    ctx.ht_op = HtOperation::FromDdk(ht_op_ddk);
    ctx.vht_cap = VhtCapabilities::FromDdk(vht_cap_ddk);
    ctx.vht_op = VhtOperation::FromDdk(vht_op_ddk);

    return ctx;
}

wlan_band_info_t FakeBandInfo(Band band) {
    ZX_DEBUG_ASSERT(band == WLAN_BAND_2GHZ || band == WLAN_BAND_5GHZ);

    // Construct a base
    wlan_band_info_t bi = {
        .band_id = static_cast<uint8_t>(band),
        .basic_rates = {12, 24, 48, 54, 96, 108},
        .supported_channels =
            {
                .base_freq = 0,
                .channels = {0},
            },
        .ht_supported = true,
        .ht_caps =
            {
                .ht_capability_info = 0x0063,
                .ampdu_params = 0x17,
                .supported_mcs_set =
                    {
                        // Rx MCS bitmask
                        // Supported MCS values: 0-7
                        // clang-format off
                        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00,
                        // Tx parameters
                        0x01, 0x00, 0x00, 0x00,
                        // clang-format on
                    },
                .ht_ext_capabilities = 0x0000,
                .tx_beamforming_capabilities = 0x00000000,
                .asel_capabilities = 0x00,
            },
        .vht_supported = true,
        .vht_caps =
            {
                .vht_capability_info = 0x0f805032,
                .supported_vht_mcs_and_nss_set = 0x0000fffe0000fffe,
            },
    };

    if (band == WLAN_BAND_5GHZ) {
        bi.supported_channels.base_freq = 5000;
        uint8_t fake[WLAN_CHANNELS_MAX_LEN] = {36, 40, 44, 48, 149, 153, 157, 161};
        memcpy(bi.supported_channels.channels, fake, WLAN_CHANNELS_MAX_LEN * sizeof(uint8_t));
    } else {
        bi.supported_channels.base_freq = 2407;
        uint8_t fake[WLAN_CHANNELS_MAX_LEN] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
        memcpy(bi.supported_channels.channels, fake, WLAN_CHANNELS_MAX_LEN * sizeof(uint8_t));

        bi.vht_supported = false;
        bi.vht_caps = wlan_vht_caps_t{};
    }
    return bi;
}

}  // namespace test_utils
}  // namespace wlan
