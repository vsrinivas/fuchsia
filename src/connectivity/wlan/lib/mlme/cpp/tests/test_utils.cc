// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_utils.h"

#include <fuchsia/hardware/wlan/associnfo/c/banjo.h>
#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>

#include <wlan/common/channel.h>

namespace wlan {
namespace test_utils {

wlan_assoc_ctx_t FakeDdkAssocCtx() {
  return wlan_assoc_ctx_t{
      .has_ht_cap = true,
      .ht_cap =
          ieee80211_ht_capabilities{
              .ht_capability_info = 0x0162,
              .ampdu_params = 0x17,
              .supported_mcs_set =
                  ieee80211_ht_capabilities_supported_mcs_set_t{
                      .fields =
                          ieee80211_ht_capabilities_supported_mcs_set_fields{
                              .rx_mcs_head = 0x00000001000000ff,
                              .rx_mcs_tail = 0x01000000,
                              .tx_mcs = 0x00000000,
                          },
                  },
              .ht_ext_capabilities = 0x1234,
              .tx_beamforming_capabilities = 0x12345678,
              .asel_capabilities = 0xff,
          },
      .has_ht_op = true,
      .ht_op =
          wlan_ht_op_t{
              .primary_channel = 123,
              .head = 0x01020304,
              .tail = 0x05,
              .rx_mcs_head = 0x00000001000000ff,
              .rx_mcs_tail = 0x01000000,
              .tx_mcs = 0x00000000,
          },
      .has_vht_cap = true,
      .vht_cap =
          ieee80211_vht_capabilities_t{
              .vht_capability_info = 0x0f805032,
              .supported_vht_mcs_and_nss_set = 0x0000fffe0000fffe,
          },
      .has_vht_op = true,
      .vht_op =
          wlan_vht_op_t{
              .vht_cbw = 0x01,
              .center_freq_seg0 = 42,
              .center_freq_seg1 = 106,
              .basic_mcs = 0x1122,
          },
  };
}

wlan_softmac_band_capability_t FakeBandCapability(wlan_band_t band) {
  ZX_DEBUG_ASSERT(band == WLAN_BAND_TWO_GHZ || band == WLAN_BAND_FIVE_GHZ);

  // Construct a base
  wlan_softmac_band_capability_t bc = {
      .band = static_cast<uint8_t>(band),
      .basic_rate_count = 6,
      .basic_rate_list = {12, 24, 48, 54, 96, 108},
      .ht_supported = true,
      .ht_caps =
          {
              .ht_capability_info = 0x0063,
              .ampdu_params = 0x17,
              .supported_mcs_set =
                  {
                      .bytes =
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
      .operating_channel_count = 0,
      .operating_channel_list = {},
  };

  if (band == WLAN_BAND_FIVE_GHZ) {
    uint8_t fake[fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS] = {36,  40,  44,  48,
                                                                       149, 153, 157, 161};
    memcpy(bc.operating_channel_list, fake,
           fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS * sizeof(uint8_t));
    bc.operating_channel_count = 8;
  } else {
    uint8_t fake[fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS] = {1, 2, 3,  4,  5,  6,  7,
                                                                       8, 9, 10, 11, 12, 13, 14};
    memcpy(bc.operating_channel_list, fake,
           fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS * sizeof(uint8_t));
    bc.operating_channel_count = 14;

    bc.vht_supported = false;
    bc.vht_caps = {};
  }
  return bc;
}

}  // namespace test_utils
}  // namespace wlan
