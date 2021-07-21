/*
 * Copyright (c) 2020 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <zircon/errors.h>

#include <array>
#include <tuple>

#include <wlan/common/mcs_rate_lookup.h>

#include "gtest/gtest.h"

namespace wlan::common {

namespace {

namespace wlan_common = ::fuchsia::wlan::common;

// Parameters used in HT and VHT tests below.
const uint8_t kValidMcs = 0;
const uint8_t kInvalidMcs = 100;
const auto kValidGi = wlan_common::GuardInterval::LONG_GI;

// HT-specific parameters used in tests below.
const auto kValidHtCbw = wlan_common::ChannelBandwidth::CBW20;
const auto kInvalidHtCbw = wlan_common::ChannelBandwidth::CBW160;

// VHT-specific parameters used in tests below.
const auto kValidVhtCbw = wlan_common::ChannelBandwidth::CBW80;
const uint8_t kValidVhtNss = 1;
const uint8_t kInvalidVhtNss = 9;

TEST(McsRateLookup, HtLookupSucceedsFor20MhzCbw) {
  uint32_t out_kbps;
  const uint8_t mcs = 31;
  const uint32_t expected_long_gi_kbps = 260000;
  ASSERT_EQ(HtDataRateLookup(wlan_common::ChannelBandwidth::CBW20, mcs,
                             wlan_common::GuardInterval::LONG_GI, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_long_gi_kbps);

  const uint32_t expected_short_gi_kbps = 288900;
  ASSERT_EQ(HtDataRateLookup(wlan_common::ChannelBandwidth::CBW20, mcs,
                             wlan_common::GuardInterval::SHORT_GI, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_short_gi_kbps);
}

TEST(McsRateLookup, HtLookupSucceedsFor40MhzCbw) {
  uint32_t out_kbps;
  const uint8_t mcs = 17;
  const uint32_t expected_long_gi_kbps = 81000;
  ASSERT_EQ(HtDataRateLookup(wlan_common::ChannelBandwidth::CBW40, mcs,
                             wlan_common::GuardInterval::LONG_GI, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_long_gi_kbps);

  const uint32_t expected_short_gi_kbps = 90000;
  ASSERT_EQ(HtDataRateLookup(wlan_common::ChannelBandwidth::CBW40, mcs,
                             wlan_common::GuardInterval::SHORT_GI, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_short_gi_kbps);
}

TEST(McsRateLookup, HtLookupFailsWithInvalidCbw) {
  uint32_t out_kbps;
  EXPECT_EQ(HtDataRateLookup(kInvalidHtCbw, kValidMcs, kValidGi, &out_kbps), ZX_ERR_OUT_OF_RANGE);
}

TEST(McsRateLookup, HtLookupFailsWithInvalidMcs) {
  uint32_t out_kbps;
  EXPECT_EQ(HtDataRateLookup(kValidHtCbw, kInvalidMcs, kValidGi, &out_kbps), ZX_ERR_OUT_OF_RANGE);
}

TEST(McsRateLookup, HtLookupFailsWithSpecialCaseInvalidMcs) {
  uint32_t out_kbps;
  // 20 MHz MCS index 32 is a special case that is invalid.
  EXPECT_EQ(HtDataRateLookup(wlan_common::ChannelBandwidth::CBW20, 32, kValidGi, &out_kbps),
            ZX_ERR_OUT_OF_RANGE);
}

TEST(McsRateLookup, VhtLookupSucceedsFor20MhzCbw) {
  uint32_t out_kbps;
  const uint8_t mcs = 0;
  const uint8_t nss = 1;
  const uint32_t expected_long_gi_kbps = 6500;
  ASSERT_EQ(VhtDataRateLookup(wlan_common::ChannelBandwidth::CBW20, mcs,
                              wlan_common::GuardInterval::LONG_GI, nss, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_long_gi_kbps);

  const uint32_t expected_short_gi_kbps = 7200;
  ASSERT_EQ(VhtDataRateLookup(wlan_common::ChannelBandwidth::CBW20, mcs,
                              wlan_common::GuardInterval::SHORT_GI, nss, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_short_gi_kbps);
}

TEST(McsRateLookup, VhtLookupSucceedsFor40MhzCbw) {
  uint32_t out_kbps;
  const uint8_t mcs = 5;
  const uint8_t nss = 2;
  const uint32_t expected_long_gi_kbps = 216000;
  ASSERT_EQ(VhtDataRateLookup(wlan_common::ChannelBandwidth::CBW40, mcs,
                              wlan_common::GuardInterval::LONG_GI, nss, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_long_gi_kbps);

  const uint32_t expected_short_gi_kbps = 240000;
  ASSERT_EQ(VhtDataRateLookup(wlan_common::ChannelBandwidth::CBW40, mcs,
                              wlan_common::GuardInterval::SHORT_GI, nss, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_short_gi_kbps);
}

TEST(McsRateLookup, VhtLookupSucceedsFor80MhzCbw) {
  uint32_t out_kbps;
  const uint8_t mcs = 8;
  const uint8_t nss = 6;
  const uint32_t expected_long_gi_kbps = 2106000;
  ASSERT_EQ(VhtDataRateLookup(wlan_common::ChannelBandwidth::CBW80, mcs,
                              wlan_common::GuardInterval::LONG_GI, nss, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_long_gi_kbps);

  const uint32_t expected_short_gi_kbps = 2340000;
  ASSERT_EQ(VhtDataRateLookup(wlan_common::ChannelBandwidth::CBW80, mcs,
                              wlan_common::GuardInterval::SHORT_GI, nss, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_short_gi_kbps);
}

TEST(McsRateLookup, VhtLookupSucceedsFor160MhzCbw) {
  uint32_t out_kbps;
  const uint8_t mcs = 2;
  const uint8_t nss = 8;
  const uint32_t expected_long_gi_kbps = 1404000;
  ASSERT_EQ(VhtDataRateLookup(wlan_common::ChannelBandwidth::CBW160, mcs,
                              wlan_common::GuardInterval::LONG_GI, nss, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_long_gi_kbps);

  const uint32_t expected_short_gi_kbps = 1560000;
  ASSERT_EQ(VhtDataRateLookup(wlan_common::ChannelBandwidth::CBW160, mcs,
                              wlan_common::GuardInterval::SHORT_GI, nss, &out_kbps),
            ZX_OK);
  EXPECT_EQ(out_kbps, expected_short_gi_kbps);
}

TEST(McsRateLookup, VhtLookupFailsWithInvalidMcs) {
  uint32_t out_kbps;
  EXPECT_EQ(VhtDataRateLookup(kValidVhtCbw, kInvalidMcs, kValidGi, kValidVhtNss, &out_kbps),
            ZX_ERR_OUT_OF_RANGE);
}

TEST(McsRateLookup, VhtLookupFailsWithSpecialCaseInvalidMcs) {
  const uint8_t invalid_count = 9;
  // Tuple fields are CBW, NSS, MCS:
  using invalid_param_t = std::tuple<wlan_common::ChannelBandwidth, uint8_t, uint8_t>;
  const std::array<invalid_param_t, invalid_count> invalidVhtParams = {
      std::make_tuple(wlan_common::ChannelBandwidth::CBW20, 1, 9),
      std::make_tuple(wlan_common::ChannelBandwidth::CBW20, 2, 9),
      std::make_tuple(wlan_common::ChannelBandwidth::CBW20, 4, 9),
      std::make_tuple(wlan_common::ChannelBandwidth::CBW20, 5, 9),
      std::make_tuple(wlan_common::ChannelBandwidth::CBW20, 7, 9),
      std::make_tuple(wlan_common::ChannelBandwidth::CBW20, 8, 9),
      std::make_tuple(wlan_common::ChannelBandwidth::CBW80, 6, 9),
      std::make_tuple(wlan_common::ChannelBandwidth::CBW80, 32, 1),
      std::make_tuple(wlan_common::ChannelBandwidth::CBW160, 3, 9),
  };
  for (const auto& params : invalidVhtParams) {
    const auto& cbw = std::get<0>(params);
    const auto& nss = std::get<1>(params);
    const auto& mcs = std::get<2>(params);
    uint32_t out_kbps;
    EXPECT_EQ(VhtDataRateLookup(cbw, mcs, wlan_common::GuardInterval::LONG_GI, nss, &out_kbps),
              ZX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(VhtDataRateLookup(cbw, mcs, wlan_common::GuardInterval::SHORT_GI, nss, &out_kbps),
              ZX_ERR_OUT_OF_RANGE);
  }
}

TEST(McsRateLookup, VhtLookupFailsWithInvalidNss) {
  uint32_t out_kbps;
  EXPECT_EQ(VhtDataRateLookup(kValidVhtCbw, kInvalidMcs, kValidGi, kInvalidVhtNss, &out_kbps),
            ZX_ERR_OUT_OF_RANGE);
}

}  // namespace

}  // namespace wlan::common
