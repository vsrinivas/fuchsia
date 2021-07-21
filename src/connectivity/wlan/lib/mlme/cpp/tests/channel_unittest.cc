// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <gtest/gtest.h>
#include <wlan/common/channel.h>

namespace wlan {
namespace common {
namespace {

namespace wlan_common = ::fuchsia::wlan::common;

class ChannelTest : public ::testing::Test {
 protected:
};

TEST_F(ChannelTest, ValidCombo) {
  std::vector<wlan_channel_t> tvs = {
      // clang-format off
        {  1, CHANNEL_BANDWIDTH_CBW20,        0},
        { 11, CHANNEL_BANDWIDTH_CBW20,        0},
        {  1, CHANNEL_BANDWIDTH_CBW40,   0},
        {  6, CHANNEL_BANDWIDTH_CBW40,   0},
        {  6, CHANNEL_BANDWIDTH_CBW40BELOW,   0},
        { 11, CHANNEL_BANDWIDTH_CBW40BELOW,   0},
        { 36, CHANNEL_BANDWIDTH_CBW40,   0},
        { 40, CHANNEL_BANDWIDTH_CBW40BELOW,   0},
        {100, CHANNEL_BANDWIDTH_CBW40,   0},
        {104, CHANNEL_BANDWIDTH_CBW40BELOW,   0},
        {149, CHANNEL_BANDWIDTH_CBW40,   0},
        {153, CHANNEL_BANDWIDTH_CBW40BELOW,   0},
        { 36, CHANNEL_BANDWIDTH_CBW80,        0},
        { 40, CHANNEL_BANDWIDTH_CBW80,        0},
        {100, CHANNEL_BANDWIDTH_CBW80,        0},
        {149, CHANNEL_BANDWIDTH_CBW80,        0},
        {161, CHANNEL_BANDWIDTH_CBW80,        0},
        { 36, CHANNEL_BANDWIDTH_CBW80P80,   106},
        { 52, CHANNEL_BANDWIDTH_CBW80P80,   106},
        {100, CHANNEL_BANDWIDTH_CBW80P80,    42},
        {149, CHANNEL_BANDWIDTH_CBW80P80,    42},
        {161, CHANNEL_BANDWIDTH_CBW80P80,    42},
        { 36, CHANNEL_BANDWIDTH_CBW160,       0},
        {100, CHANNEL_BANDWIDTH_CBW160,       0},
      // clang-format on
  };

  for (auto tv : tvs) {
    EXPECT_TRUE(IsValidChan(tv));
  }
}

TEST_F(ChannelTest, Equality) {
  wlan_channel_t lhs{.primary = 1, .cbw = CHANNEL_BANDWIDTH_CBW20};
  wlan_channel_t rhs{.primary = 1, .cbw = CHANNEL_BANDWIDTH_CBW20};
  EXPECT_EQ(true, lhs == rhs);

  rhs.cbw = CHANNEL_BANDWIDTH_CBW40;
  EXPECT_EQ(true, lhs != rhs);

  lhs.cbw = CHANNEL_BANDWIDTH_CBW40;
  EXPECT_EQ(true, lhs == rhs);

  lhs.cbw = CHANNEL_BANDWIDTH_CBW40;
  EXPECT_EQ(true, lhs == rhs);

  rhs.cbw = CHANNEL_BANDWIDTH_CBW40BELOW;
  EXPECT_EQ(false, lhs == rhs);

  rhs.cbw = CHANNEL_BANDWIDTH_CBW40;
  rhs.primary = 2;
  EXPECT_EQ(true, lhs != rhs);

  lhs.primary = 2;
  EXPECT_EQ(true, lhs == rhs);
}

TEST_F(ChannelTest, InvalidCombo) {
  std::vector<wlan_channel_t> tvs = {
      // clang-format off
        {  0, CHANNEL_BANDWIDTH_CBW20,        0},
        { 15, CHANNEL_BANDWIDTH_CBW20,        0},
        {  8, CHANNEL_BANDWIDTH_CBW40,   0},
        {  4, CHANNEL_BANDWIDTH_CBW40BELOW,   0},
        { 32, CHANNEL_BANDWIDTH_CBW20,        0},
        { 68, CHANNEL_BANDWIDTH_CBW20,        0},
        { 96, CHANNEL_BANDWIDTH_CBW20,        0},
        {148, CHANNEL_BANDWIDTH_CBW20,        0},
        {183, CHANNEL_BANDWIDTH_CBW20,        0},
        { 36, CHANNEL_BANDWIDTH_CBW40BELOW,   0},
        { 40, CHANNEL_BANDWIDTH_CBW40,   0},
        {149, CHANNEL_BANDWIDTH_CBW40BELOW,   0},
        {153, CHANNEL_BANDWIDTH_CBW40,   0},
        {165, CHANNEL_BANDWIDTH_CBW80,        0},
        { 36, CHANNEL_BANDWIDTH_CBW80P80,     0},
        { 48, CHANNEL_BANDWIDTH_CBW80P80,    42},
        {149, CHANNEL_BANDWIDTH_CBW80P80,   155},
        {132, CHANNEL_BANDWIDTH_CBW160,      50},
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

TEST_F(ChannelTest, Conversion) {
  struct TestVector {
    wlan_channel_t ddk;
    wlan_common::WlanChannel fidl;
    bool is_same;
  };

  std::vector<TestVector> tvs = {
      // clang-format off
        {{  0, CHANNEL_BANDWIDTH_CBW20,      0}, {  0, wlan_common::ChannelBandwidth::CBW20,      0}, true,},
        {{  1, CHANNEL_BANDWIDTH_CBW20,      0}, { 11, wlan_common::ChannelBandwidth::CBW20,      0}, false,},
        {{ 11, CHANNEL_BANDWIDTH_CBW40BELOW, 0}, { 11, wlan_common::ChannelBandwidth::CBW20,      0}, false,},
        {{ 36, CHANNEL_BANDWIDTH_CBW20,      0}, { 36, wlan_common::ChannelBandwidth::CBW40,      0}, false,},
        {{ 36, CHANNEL_BANDWIDTH_CBW40,      0}, { 36, wlan_common::ChannelBandwidth::CBW20,      0}, false,},
        {{ 36, CHANNEL_BANDWIDTH_CBW40,      0}, { 36, wlan_common::ChannelBandwidth::CBW80,      0}, false,},
        {{ 36, CHANNEL_BANDWIDTH_CBW40,      0}, { 36, wlan_common::ChannelBandwidth::CBW160,     0}, false,},
        {{ 36, CHANNEL_BANDWIDTH_CBW40,    155}, { 36, wlan_common::ChannelBandwidth::CBW80P80, 155}, false,},
        {{169, CHANNEL_BANDWIDTH_CBW160,     0}, {169, wlan_common::ChannelBandwidth::CBW160,     0}, true,},
        {{  6, CHANNEL_BANDWIDTH_CBW40,      0}, {  6, wlan_common::ChannelBandwidth::CBW40,      0}, true,},
        {{  6, CHANNEL_BANDWIDTH_CBW40,      0}, {  6, wlan_common::ChannelBandwidth::CBW40BELOW, 0}, false,},
      // clang-format on
  };

  for (auto tv : tvs) {
    auto got_fidl = ToFidl(tv.ddk);
    EXPECT_EQ(tv.is_same, fidl::Equals(tv.fidl, got_fidl));

    auto got_ddk = FromFidl(tv.fidl);
    EXPECT_EQ(tv.is_same, tv.ddk == got_ddk);
  }
}

TEST_F(ChannelTest, GetCenterChanIdx) {
  struct TestVector {
    wlan_channel_t ddk;
    uint8_t want;
  };

  std::vector<TestVector> tvs = {
      // clang-format off
        {{  1, CHANNEL_BANDWIDTH_CBW20,      0},   1},
        {{ 11, CHANNEL_BANDWIDTH_CBW20,      0},  11},
        {{ 36, CHANNEL_BANDWIDTH_CBW20,      0},  36},
        {{161, CHANNEL_BANDWIDTH_CBW20,      0}, 161},
        {{  1, CHANNEL_BANDWIDTH_CBW40, 0},   3},
        {{  5, CHANNEL_BANDWIDTH_CBW40, 0},   7},
        {{  5, CHANNEL_BANDWIDTH_CBW40BELOW, 0},   3},
        {{ 11, CHANNEL_BANDWIDTH_CBW40BELOW, 0},   9},
        {{ 36, CHANNEL_BANDWIDTH_CBW40, 0},  38},
        {{ 36, CHANNEL_BANDWIDTH_CBW80,      0},  42},
        {{104, CHANNEL_BANDWIDTH_CBW80,      0}, 106},
        {{ 36, CHANNEL_BANDWIDTH_CBW80P80, 122},  42},
        {{ 36, CHANNEL_BANDWIDTH_CBW160,     0},  50},
        {{100, CHANNEL_BANDWIDTH_CBW160,     0}, 114}
      // clang-format on
  };

  for (auto tv : tvs) {
    auto got = GetCenterChanIdx(tv.ddk);
    EXPECT_EQ(tv.want, got);
  }
}

TEST_F(ChannelTest, GetCenterFreq) {
  struct TestVector {
    wlan_channel_t ddk;
    Mhz want;
  };

  std::vector<TestVector> tvs = {
      // clang-format off
        {{  1, CHANNEL_BANDWIDTH_CBW20,      0}, 2412},
        {{  1, CHANNEL_BANDWIDTH_CBW40, 0}, 2422},
        {{  6, CHANNEL_BANDWIDTH_CBW40, 0}, 2447},
        {{  6, CHANNEL_BANDWIDTH_CBW40BELOW, 0}, 2427},
        {{ 11, CHANNEL_BANDWIDTH_CBW20,      0}, 2462},
        {{ 11, CHANNEL_BANDWIDTH_CBW40BELOW, 0}, 2452},
        {{ 36, CHANNEL_BANDWIDTH_CBW20,      0}, 5180},
        {{ 36, CHANNEL_BANDWIDTH_CBW40, 0}, 5190},
        {{ 36, CHANNEL_BANDWIDTH_CBW80,      0}, 5210},
        {{ 36, CHANNEL_BANDWIDTH_CBW160,     0}, 5250},
        {{161, CHANNEL_BANDWIDTH_CBW20,      0}, 5805},
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
