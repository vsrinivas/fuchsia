// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_utils.h"

#include <fuchsia/hardware/wlan/associnfo/c/banjo.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>

#include <wlan/common/channel.h>

namespace wlan {
namespace test_utils {

wlan_assoc_ctx_t FakeDdkAssocCtx() {
  return wlan_assoc_ctx_t{
      .has_ht_cap = true,
      .ht_cap =
          ht_capabilities_t{
              .bytes =
                  {
                      0x62, 0x01,  // HT capability info
                      0x17,        // AMPDU params
                      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xff,
                      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Supported MCS set
                      0x34, 0x12,                                      // HT extended capabilities
                      0x78, 0x56, 0x34, 0x12,  // TX beamforming capabilities,
                      0xff,                    // ASEL capabilities
                  },
          },
      .has_ht_op = true,
      .ht_op =
          wlan_ht_op_t{
              .primary_channel = 123,
              .head = 0x01020304,
              .tail = 0x05,
              .mcs_set = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                          0x00, 0x00, 0x00, 0xff},
          },
      .has_vht_cap = true,
      .vht_cap =
          vht_capabilities_t{
              .bytes =
                  {
                      0x32, 0x50, 0x80, 0x0f,                          // VHT capability info
                      0xfe, 0xff, 0x00, 0x00, 0xfe, 0xff, 0x00, 0x00,  // VHT MCS and NSS set
                  },
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
              .bytes =
                  {
                      0x63, 0x00,  // HT capability info
                      0x17,        // AMPDU params
                      // Rx MCS bitmask
                      0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00,                    // Supported MCS values: 0-7
                      0x01, 0x00, 0x00, 0x00,  // Tx parameters
                      0x00, 0x00,              // HT extended capabilities
                      0x00, 0x00, 0x00, 0x00,  // TX beamforming capabilities
                      0x00,                    // ASEL capabilities

                  },
          },
      .vht_supported = true,
      .vht_caps =
          {
              .bytes =
                  {
                      0x32, 0x50, 0x80, 0x0f,                          // VHT capability info
                      0xfe, 0xff, 0x00, 0x00, 0xfe, 0xff, 0x00, 0x00,  // VHT MCS and NSS set
                  },
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
