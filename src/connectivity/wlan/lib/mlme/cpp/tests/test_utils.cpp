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
  return AssocContext{
      .ht_cap =
          HtCapabilities{
              .ht_cap_info = HtCapabilityInfo(0x0162),
              .ampdu_params = AmpduParams(0x17),
              .mcs_set =
                  SupportedMcsSet{
                      .rx_mcs_head = SupportedMcsRxMcsHead(0x00000001000000ff),
                      .rx_mcs_tail = SupportedMcsRxMcsTail(0x01000000),
                      .tx_mcs = SupportedMcsTxMcs(0x00000000),
                  },
              .ht_ext_cap = HtExtCapabilities(0x1234),
              .txbf_cap = TxBfCapability(0x12345678),
              .asel_cap = AselCapability(0xff),
          },
      .ht_op =
          HtOperation{
              .primary_chan = 123,
              .head = HtOpInfoHead(0x01020304),
              .tail = HtOpInfoTail(0x05),
              .basic_mcs_set =
                  SupportedMcsSet{
                      .rx_mcs_head = SupportedMcsRxMcsHead(0x00000001000000ff),
                      .rx_mcs_tail = SupportedMcsRxMcsTail(0x01000000),
                      .tx_mcs = SupportedMcsTxMcs(0x00000000),
                  },
          },
      .vht_cap =
          VhtCapabilities{
              .vht_cap_info = VhtCapabilitiesInfo(0x0f805032),
              .vht_mcs_nss = VhtMcsNss(0x0000fffe0000fffe),
          },
      .vht_op =
          VhtOperation{
              .vht_cbw = 0x01,
              .center_freq_seg0 = 42,
              .center_freq_seg1 = 106,
              .basic_mcs = BasicVhtMcsNss(0x1122),
          },
  };
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
    memcpy(bi.supported_channels.channels, fake,
           WLAN_CHANNELS_MAX_LEN * sizeof(uint8_t));
  } else {
    bi.supported_channels.base_freq = 2407;
    uint8_t fake[WLAN_CHANNELS_MAX_LEN] = {1, 2, 3,  4,  5,  6,  7,
                                           8, 9, 10, 11, 12, 13, 14};
    memcpy(bi.supported_channels.channels, fake,
           WLAN_CHANNELS_MAX_LEN * sizeof(uint8_t));

    bi.vht_supported = false;
    bi.vht_caps = wlan_vht_caps_t{};
  }
  return bi;
}

}  // namespace test_utils
}  // namespace wlan
