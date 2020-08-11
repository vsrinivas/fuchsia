// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_utils.h"

#include <ddk/protocol/wlan/info.h>
#include <wlan/common/channel.h>
#include <wlan/protocol/mac.h>

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
              .primary_chan = 123,
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

wlan_info_band_info_t FakeBandInfo(wlan_info_band_t band) {
  ZX_DEBUG_ASSERT(band == WLAN_INFO_BAND_2GHZ || band == WLAN_INFO_BAND_5GHZ);

  // Construct a base
  wlan_info_band_info_t bi = {
      .band = static_cast<uint8_t>(band),
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
      .rates = {12, 24, 48, 54, 96, 108},
      .supported_channels =
          {
              .base_freq = 0,
              .channels = {0},
          },
  };

  if (band == WLAN_INFO_BAND_5GHZ) {
    bi.supported_channels.base_freq = 5000;
    uint8_t fake[WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS] = {36, 40, 44, 48, 149, 153, 157, 161};
    memcpy(bi.supported_channels.channels, fake,
           WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS * sizeof(uint8_t));
  } else {
    bi.supported_channels.base_freq = 2407;
    uint8_t fake[WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS] = {1, 2, 3,  4,  5,  6,  7,
                                                         8, 9, 10, 11, 12, 13, 14};
    memcpy(bi.supported_channels.channels, fake,
           WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS * sizeof(uint8_t));

    bi.vht_supported = false;
    bi.vht_caps = {};
  }
  return bi;
}

}  // namespace test_utils
}  // namespace wlan
