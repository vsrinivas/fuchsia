// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/common/cpp/fidl.h>
#include <fuchsia/wlan/device/cpp/fidl.h>

#include <gtest/gtest.h>

#include "../device.h"

namespace wlanphy {
namespace {
namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_device = ::fuchsia::wlan::device;

TEST(WlanphyTest, ConvertPhyBandInfo) {
  wlan_info_band_info_t in[WLAN_INFO_MAX_BANDS];
  in[0] = {};
  in[1] = {};

  in[0].band = WLAN_INFO_BAND_2GHZ;
  in[1].band = WLAN_INFO_BAND_5GHZ;

  in[0].ht_supported = false;
  in[1].ht_supported = true;

  in[0].vht_supported = false;
  in[1].vht_supported = true;

  for (size_t i = 0; i < 10; i++) {
    in[0].rates[i] = i + 1;
    in[1].rates[i] = 101 + i;
  }
  in[0].rates[10] = 11;

  in[0].supported_channels.base_freq = 65533;
  in[1].supported_channels.base_freq = 65534;

  for (uint8_t i = 0; i < WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS; i++) {
    if (i < 32) {
      in[0].supported_channels.channels[i] = 11 + i;
    }
    in[1].supported_channels.channels[i] = 22 + i;
  }

  std::vector<wlan_device::BandInfo> out;
  ConvertPhyBandInfo(&out, WLAN_INFO_MAX_BANDS, in);
  ASSERT_EQ(out.size(), 2ul);
  EXPECT_EQ(out[0].band_id, wlan_common::Band::WLAN_BAND_2GHZ);
  EXPECT_EQ(out[1].band_id, wlan_common::Band::WLAN_BAND_5GHZ);
  EXPECT_EQ(out[0].ht_caps.get(), nullptr);
  EXPECT_NE(out[1].ht_caps.get(), nullptr);
  EXPECT_EQ(out[0].vht_caps.get(), nullptr);
  EXPECT_NE(out[1].vht_caps.get(), nullptr);
  std::vector<uint8_t> expected_rates_2g = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  std::vector<uint8_t> expected_rates_5g = {101, 102, 103, 104, 105, 106, 107, 108, 109, 110};
  EXPECT_EQ(out[0].rates, expected_rates_2g);
  EXPECT_EQ(out[1].rates, expected_rates_5g);

  EXPECT_EQ(out[0].supported_channels.base_freq, 65533);
  EXPECT_EQ(out[1].supported_channels.base_freq, 65534);
  std::vector<uint8_t> expected_channels_2g = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                               22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
                                               33, 34, 35, 36, 37, 38, 39, 40, 41, 42};
  std::vector<uint8_t> expected_channels_5g = {
      22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
      44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
      66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85};
  EXPECT_EQ(out[0].supported_channels.channels, expected_channels_2g);
  EXPECT_EQ(out[1].supported_channels.channels, expected_channels_5g);
}
}  // namespace
}  // namespace wlanphy
