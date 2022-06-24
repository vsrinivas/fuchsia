// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.wlan.common/cpp/wire_types.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <gtest/gtest.h>
#include <wlan/common/channel.h>

namespace wlan {
namespace common {
namespace {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_common_wire = ::fuchsia_wlan_common::wire;

class ChannelTest : public ::testing::Test {
 protected:
};

TEST_F(ChannelTest, ValidCombo) {
  std::vector<wlan_common_wire::WlanChannel> tvs = {
      // clang-format off
        {  1, wlan_common_wire::ChannelBandwidth::kCbw20,        0},
        { 11, wlan_common_wire::ChannelBandwidth::kCbw20,        0},
        {  1, wlan_common_wire::ChannelBandwidth::kCbw40,   0},
        {  6, wlan_common_wire::ChannelBandwidth::kCbw40,   0},
        {  6, wlan_common_wire::ChannelBandwidth::kCbw40Below,   0},
        { 11, wlan_common_wire::ChannelBandwidth::kCbw40Below,   0},
        { 36, wlan_common_wire::ChannelBandwidth::kCbw40,   0},
        { 40, wlan_common_wire::ChannelBandwidth::kCbw40Below,   0},
        {100, wlan_common_wire::ChannelBandwidth::kCbw40,   0},
        {104, wlan_common_wire::ChannelBandwidth::kCbw40Below,   0},
        {149, wlan_common_wire::ChannelBandwidth::kCbw40,   0},
        {153, wlan_common_wire::ChannelBandwidth::kCbw40Below,   0},
        { 36, wlan_common_wire::ChannelBandwidth::kCbw80,        0},
        { 40, wlan_common_wire::ChannelBandwidth::kCbw80,        0},
        {100, wlan_common_wire::ChannelBandwidth::kCbw80,        0},
        {149, wlan_common_wire::ChannelBandwidth::kCbw80,        0},
        {161, wlan_common_wire::ChannelBandwidth::kCbw80,        0},
        { 36, wlan_common_wire::ChannelBandwidth::kCbw80P80,   106},
        { 52, wlan_common_wire::ChannelBandwidth::kCbw80P80,   106},
        {100, wlan_common_wire::ChannelBandwidth::kCbw80P80,    42},
        {149, wlan_common_wire::ChannelBandwidth::kCbw80P80,    42},
        {161, wlan_common_wire::ChannelBandwidth::kCbw80P80,    42},
        { 36, wlan_common_wire::ChannelBandwidth::kCbw160,       0},
        {100, wlan_common_wire::ChannelBandwidth::kCbw160,       0},
      // clang-format on
  };

  for (auto tv : tvs) {
    EXPECT_TRUE(IsValidChan((tv)));
  }
}

TEST_F(ChannelTest, Equality) {
  wlan_common_wire::WlanChannel lhs{.primary = 1,
                                    .cbw = wlan_common_wire::ChannelBandwidth::kCbw20};
  wlan_common_wire::WlanChannel rhs{.primary = 1,
                                    .cbw = wlan_common_wire::ChannelBandwidth::kCbw20};
  EXPECT_EQ(true, lhs == rhs);

  rhs.cbw = wlan_common_wire::ChannelBandwidth::kCbw40;
  EXPECT_EQ(true, lhs != rhs);

  lhs.cbw = wlan_common_wire::ChannelBandwidth::kCbw40;
  EXPECT_EQ(true, lhs == rhs);

  lhs.cbw = wlan_common_wire::ChannelBandwidth::kCbw40;
  EXPECT_EQ(true, lhs == rhs);

  rhs.cbw = wlan_common_wire::ChannelBandwidth::kCbw40Below;
  EXPECT_EQ(false, lhs == rhs);

  rhs.cbw = wlan_common_wire::ChannelBandwidth::kCbw40;
  rhs.primary = 2;
  EXPECT_EQ(true, lhs != rhs);

  lhs.primary = 2;
  EXPECT_EQ(true, lhs == rhs);
}

TEST_F(ChannelTest, InvalidCombo) {
  std::vector<wlan_common_wire::WlanChannel> tvs = {
      // clang-format off
        {  0, wlan_common_wire::ChannelBandwidth::kCbw20,        0},
        { 15, wlan_common_wire::ChannelBandwidth::kCbw20,        0},
        {  8, wlan_common_wire::ChannelBandwidth::kCbw40,   0},
        {  4, wlan_common_wire::ChannelBandwidth::kCbw40Below,   0},
        { 32, wlan_common_wire::ChannelBandwidth::kCbw20,        0},
        { 68, wlan_common_wire::ChannelBandwidth::kCbw20,        0},
        { 96, wlan_common_wire::ChannelBandwidth::kCbw20,        0},
        {148, wlan_common_wire::ChannelBandwidth::kCbw20,        0},
        {183, wlan_common_wire::ChannelBandwidth::kCbw20,        0},
        { 36, wlan_common_wire::ChannelBandwidth::kCbw40Below,   0},
        { 40, wlan_common_wire::ChannelBandwidth::kCbw40,   0},
        {149, wlan_common_wire::ChannelBandwidth::kCbw40Below,   0},
        {153, wlan_common_wire::ChannelBandwidth::kCbw40,   0},
        {165, wlan_common_wire::ChannelBandwidth::kCbw80,        0},
        { 36, wlan_common_wire::ChannelBandwidth::kCbw80P80,     0},
        { 48, wlan_common_wire::ChannelBandwidth::kCbw80P80,    42},
        {149, wlan_common_wire::ChannelBandwidth::kCbw80P80,   155},
        {132, wlan_common_wire::ChannelBandwidth::kCbw160,      50},
        // Add more interesting cases
      // clang-format on
  };

  for (auto tv : tvs) {
    if (IsValidChan(tv)) {
      printf("Test failed: Should treat this channel invalid:: %s\n",
             wlan::common::ChanStrLong(tv).c_str());
    }
    EXPECT_FALSE(IsValidChan(tv));
  }
}

TEST_F(ChannelTest, GetCenterChanIdx) {
  struct TestVector {
    wlan_common_wire::WlanChannel ddk;
    uint8_t want;
  };

  std::vector<TestVector> tvs = {
      // clang-format off
        {{  1, wlan_common_wire::ChannelBandwidth::kCbw20,      0},   1},
        {{ 11, wlan_common_wire::ChannelBandwidth::kCbw20,      0},  11},
        {{ 36, wlan_common_wire::ChannelBandwidth::kCbw20,      0},  36},
        {{161, wlan_common_wire::ChannelBandwidth::kCbw20,      0}, 161},
        {{  1, wlan_common_wire::ChannelBandwidth::kCbw40, 0},   3},
        {{  5, wlan_common_wire::ChannelBandwidth::kCbw40, 0},   7},
        {{  5, wlan_common_wire::ChannelBandwidth::kCbw40Below, 0},   3},
        {{ 11, wlan_common_wire::ChannelBandwidth::kCbw40Below, 0},   9},
        {{ 36, wlan_common_wire::ChannelBandwidth::kCbw40, 0},  38},
        {{ 36, wlan_common_wire::ChannelBandwidth::kCbw80,      0},  42},
        {{104, wlan_common_wire::ChannelBandwidth::kCbw80,      0}, 106},
        {{ 36, wlan_common_wire::ChannelBandwidth::kCbw80P80, 122},  42},
        {{ 36, wlan_common_wire::ChannelBandwidth::kCbw160,     0},  50},
        {{100, wlan_common_wire::ChannelBandwidth::kCbw160,     0}, 114}
      // clang-format on
  };

  for (auto tv : tvs) {
    auto got = GetCenterChanIdx(tv.ddk);
    EXPECT_EQ(tv.want, got);
  }
}

TEST_F(ChannelTest, GetCenterFreq) {
  struct TestVector {
    wlan_common_wire::WlanChannel ddk;
    Mhz want;
  };

  std::vector<TestVector> tvs = {
      // clang-format off
        {{  1, wlan_common_wire::ChannelBandwidth::kCbw20,      0}, 2412},
        {{  1, wlan_common_wire::ChannelBandwidth::kCbw40, 0}, 2422},
        {{  6, wlan_common_wire::ChannelBandwidth::kCbw40, 0}, 2447},
        {{  6, wlan_common_wire::ChannelBandwidth::kCbw40Below, 0}, 2427},
        {{ 11, wlan_common_wire::ChannelBandwidth::kCbw20,      0}, 2462},
        {{ 11, wlan_common_wire::ChannelBandwidth::kCbw40Below, 0}, 2452},
        {{ 36, wlan_common_wire::ChannelBandwidth::kCbw20,      0}, 5180},
        {{ 36, wlan_common_wire::ChannelBandwidth::kCbw40, 0}, 5190},
        {{ 36, wlan_common_wire::ChannelBandwidth::kCbw80,      0}, 5210},
        {{ 36, wlan_common_wire::ChannelBandwidth::kCbw160,     0}, 5250},
        {{161, wlan_common_wire::ChannelBandwidth::kCbw20,      0}, 5805},
      // clang-format on
  };

  for (auto tv : tvs) {
    auto got = GetCenterFreq(tv.ddk);
    EXPECT_EQ(tv.want, got);
  }
}

}  // namespace
}  // namespace common
}  // namespace wlan
