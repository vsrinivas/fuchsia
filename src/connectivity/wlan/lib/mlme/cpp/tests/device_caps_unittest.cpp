// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/mlme/device_caps.h>

namespace wlan {

TEST(FindBandByChannel, OneBands) {
  constexpr wlan_info_t info = {
      .bands =
          {
              {
                  .supported_channels =
                      {
                          .channels = {1, 2, 3},
                      },
              },
              {
                  // Still fill out the second band with "garbage":
                  .supported_channels =
                      {
                          .channels = {4, 5, 6, 7},
                      },
              },
          },
      .num_bands = 1,
  };
  EXPECT_EQ(&info.bands[0], FindBandByChannel(info, 3));
  EXPECT_EQ(nullptr, FindBandByChannel(info, 4));
  EXPECT_EQ(nullptr, FindBandByChannel(info, 10));
}

TEST(FindBandByChannel, TwoBands) {
  constexpr static wlan_info_t info = {
      .bands =
          {
              {
                  .supported_channels =
                      {
                          .channels = {1, 2, 3},
                      },
              },
              {
                  .supported_channels =
                      {
                          .channels = {4, 5, 6, 7},
                      },
              },
          },
      .num_bands = 2,
  };

  EXPECT_EQ(&info.bands[0], FindBandByChannel(info, 3));
  EXPECT_EQ(&info.bands[1], FindBandByChannel(info, 4));
  EXPECT_EQ(nullptr, FindBandByChannel(info, 10));
}

TEST(GetRatesByChannel, SimpleTest) {
  constexpr static wlan_info_t info = {
      .bands =
          {
              {
                  .supported_channels =
                      {
                          .channels = {1, 2, 3},
                      },
                  .basic_rates = {10, 20, 30},
              },
              {
                  .supported_channels =
                      {
                          .channels = {4, 5, 6, 7},
                      },
                  .basic_rates = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110,
                                  120},
              },
          },
      .num_bands = 2,
  };

  auto rates = GetRatesByChannel(info, 2);
  EXPECT_EQ(info.bands[0].basic_rates, rates.data());
  EXPECT_EQ(3u, rates.size());

  rates = GetRatesByChannel(info, 5);
  EXPECT_EQ(info.bands[1].basic_rates, rates.data());
  EXPECT_EQ(static_cast<size_t>(WLAN_BASIC_RATES_MAX_LEN), rates.size());

  rates = GetRatesByChannel(info, 17);
  EXPECT_EQ(nullptr, rates.data());
  EXPECT_EQ(0u, rates.size());
}

}  // namespace wlan
