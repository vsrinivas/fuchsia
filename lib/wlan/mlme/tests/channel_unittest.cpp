// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <wlan/common/channel.h>

namespace wlan {
namespace common {
namespace {

class ChannelTest : public ::testing::Test {
   protected:
};

struct TestVector {
    uint8_t primary;
    uint8_t cbw;
    bool want;
};

TEST_F(ChannelTest, ValidCombo) {
    TestVector tvs[] = {
        // clang-format off
        {1, CBW20, true},
        {11, CBW20, true},
        {1, CBW40ABOVE, true},
        {6, CBW40ABOVE, true},
        {6, CBW40BELOW, true},
        {11, CBW40BELOW, true},
        {36, CBW40ABOVE, true},
        {40, CBW40BELOW, true},
        {100, CBW40ABOVE, true},
        {104, CBW40BELOW, true},
        {149, CBW40ABOVE, true},
        {153, CBW40BELOW, true},
        // Add more interesting cases
        // clang-format on
    };

    for (uint32_t i = 0; i < sizeof(tvs) / sizeof(TestVector); i++) {
        wlan_channel_t chan;
        chan = wlan_channel_t{
            .primary = tvs[i].primary,
            .cbw = tvs[i].cbw,
        };
        bool want = tvs[i].want;

        EXPECT_EQ(want, IsValidChan(chan));
    }
}
TEST_F(ChannelTest, InvalidCombo) {
    TestVector tvs[] = {
        // clang-format off
        {0, CBW20, false},
        {12, CBW20, false},
        {8, CBW40ABOVE, false},
        {4, CBW40BELOW, false},
        {32, CBW20, false},
        {68, CBW20, false},
        {96, CBW20, false},
        {148, CBW20, false},
        {169, CBW20, false},
        {36, CBW40BELOW, false},
        {40, CBW40ABOVE, false},
        {149, CBW40BELOW, false},
        {153, CBW40ABOVE, false},
        // Add more interesting cases
        // clang-format on
    };

    for (uint32_t i = 0; i < sizeof(tvs) / sizeof(TestVector); i++) {
        wlan_channel_t chan;
        chan = wlan_channel_t{
            .primary = tvs[i].primary,
            .cbw = tvs[i].cbw,
        };
        bool want = tvs[i].want;

        EXPECT_EQ(want, IsValidChan(chan));
    }
}

}  // namespace
}  // namespace common
}  // namespace wlan
