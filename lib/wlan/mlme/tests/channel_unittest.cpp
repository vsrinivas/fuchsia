// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/channel.h>

namespace wlan {
namespace common {
namespace {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

class ChannelTest : public ::testing::Test {
   protected:
};

struct TestVector {
    uint8_t primary;
    uint8_t cbw;
    bool want;
};

struct TestVectorCbw {
    uint8_t primary;
    uint8_t cbw;
    uint8_t want;
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
        {36, CBW80, true},
        {40, CBW80, true},
        {100, CBW80, true},
        {149, CBW80, true},
        {161, CBW80, true},
        // Add more interesting cases
        // clang-format on
    };

    for (uint32_t i = 0; i < sizeof(tvs) / sizeof(TestVector); i++) {
        wlan_channel_t chan = {
            .primary = tvs[i].primary,
            .cbw = tvs[i].cbw,
        };
        bool want = tvs[i].want;

        EXPECT_EQ(want, IsValidChan(chan));
    }
}

TEST_F(ChannelTest, Equality) {
    wlan_channel_t lhs{.primary = 1, .cbw = CBW20};
    wlan_channel_t rhs{.primary = 1, .cbw = CBW20};
    EXPECT_EQ(true, lhs == rhs);

    rhs.cbw = CBW40;
    EXPECT_EQ(true, lhs != rhs);

    lhs.cbw = CBW40;
    EXPECT_EQ(true, lhs == rhs);

    lhs.cbw = CBW40ABOVE;
    EXPECT_EQ(true, lhs == rhs);

    rhs.cbw = CBW40BELOW;
    EXPECT_EQ(false, lhs == rhs);

    rhs.cbw = CBW40;
    rhs.primary = 2;
    EXPECT_EQ(true, lhs != rhs);

    lhs.primary = 2;
    EXPECT_EQ(true, lhs == rhs);
}

TEST_F(ChannelTest, InvalidCombo) {
    std::vector<TestVector> tvs = {
        // clang-format off
        {0, CBW20, false},
        {15, CBW20, false},
        {8, CBW40ABOVE, false},
        {4, CBW40BELOW, false},
        {32, CBW20, false},
        {68, CBW20, false},
        {96, CBW20, false},
        {148, CBW20, false},
        {169, CBW20, true},
        {183, CBW20, false},
        {36, CBW40BELOW, false},
        {40, CBW40ABOVE, false},
        {149, CBW40BELOW, false},
        {153, CBW40ABOVE, false},
        {165, CBW80, false},
        // Add more interesting cases
        // clang-format on
    };

    for (TestVector tv : tvs) {
        wlan_channel_t chan = {
            .primary = tv.primary,
            .cbw = tv.cbw,
        };

        EXPECT_EQ(tv.want, IsValidChan(chan));
    }
}

TEST_F(ChannelTest, GetValidCbw) {
    std::vector<TestVectorCbw> tvs = {
        // clang-format off
        {1, CBW20, CBW20},
        {1, CBW40ABOVE, CBW40ABOVE},
        {1, CBW40BELOW, CBW40ABOVE},
        {9, CBW40ABOVE, CBW40BELOW},
        {9, CBW40BELOW, CBW40BELOW},
        {36, CBW40ABOVE, CBW40ABOVE},
        {36, CBW40BELOW, CBW40ABOVE},
        {40, CBW40ABOVE, CBW40BELOW},
        {40, CBW40BELOW, CBW40BELOW},
        // Add more interesting cases
        // clang-format on
    };

    for (TestVectorCbw tv : tvs) {
        wlan_channel_t chan = {
            .primary = tv.primary,
            .cbw = tv.cbw,
        };
        EXPECT_EQ(tv.want, GetValidCbw(chan));
    }
}

TEST_F(ChannelTest, Conversion) {
    struct TestVector {
        wlan_channel_t ddk;
        wlan_mlme::WlanChan fidl;
        bool is_same;
    };

    std::vector<TestVector> tvs = {
        // clang-format off
        {{  0, CBW20,      0}, {  0, wlan_mlme::CBW::CBW20,      0}, true,},
        {{  1, CBW20,      0}, { 11, wlan_mlme::CBW::CBW20,      0}, false,},
        {{ 11, CBW40BELOW, 0}, { 11, wlan_mlme::CBW::CBW20,      0}, false,},
        {{ 36, CBW20,      0}, { 36, wlan_mlme::CBW::CBW40,      0}, false,},
        {{ 36, CBW40,      0}, { 36, wlan_mlme::CBW::CBW20,      0}, false,},
        {{ 36, CBW40,      0}, { 36, wlan_mlme::CBW::CBW80,      0}, false,},
        {{ 36, CBW40,      0}, { 36, wlan_mlme::CBW::CBW160,     0}, false,},
        {{ 36, CBW40,    155}, { 36, wlan_mlme::CBW::CBW80P80, 155}, false,},
        {{169, CBW160,     0}, {169, wlan_mlme::CBW::CBW160,     0}, true,},
        {{  6, CBW40,      0}, {  6, wlan_mlme::CBW::CBW40,      0}, true,},
        {{  6, CBW40ABOVE, 0}, {  6, wlan_mlme::CBW::CBW40,      0}, true,},
        {{  6, CBW40ABOVE, 0}, {  6, wlan_mlme::CBW::CBW40BELOW, 0}, false,},
        // clang-format on
    };

    for (auto tv : tvs) {
        auto got_fidl = ToFidl(tv.ddk);
        EXPECT_EQ(tv.is_same, tv.fidl == got_fidl);

        auto got_ddk = FromFidl(tv.fidl);
        EXPECT_EQ(tv.is_same, tv.ddk == got_ddk);
    }
}

}  // namespace
}  // namespace common
}  // namespace wlan
