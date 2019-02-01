// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/element.h>
#include <wlan/mlme/mcs.h>

#include <gtest/gtest.h>

namespace wlan {
namespace {

TEST(McsTest, Intersect) {
    SupportedMcsSet lhs;
    SupportedMcsSet rhs;
    SupportedMcsSet result;

    lhs.rx_mcs_head.set_bitmask(0xffff);
    rhs.rx_mcs_head.set_bitmask(0x00ff);
    lhs.rx_mcs_tail.set_bitmask(0x0f0f);
    rhs.rx_mcs_tail.set_bitmask(0x1fff);

    lhs.rx_mcs_tail.set_highest_rate(1023);  // Max Mbps defined
    rhs.rx_mcs_tail.set_highest_rate(543);

    lhs.tx_mcs.set_set_defined(1);
    rhs.tx_mcs.set_set_defined(1);

    lhs.tx_mcs.set_rx_diff(1);
    rhs.tx_mcs.set_rx_diff(0);

    lhs.tx_mcs.set_max_ss(3);
    rhs.tx_mcs.set_max_ss(1);

    lhs.tx_mcs.set_ueqm(0);
    rhs.tx_mcs.set_ueqm(0);

    result = IntersectMcs(lhs, rhs);
    EXPECT_EQ(0xfful, result.rx_mcs_head.bitmask());
    EXPECT_EQ(0x0f0f, result.rx_mcs_tail.bitmask());
    EXPECT_EQ(543, result.rx_mcs_tail.highest_rate());
    EXPECT_EQ(1, result.tx_mcs.set_defined());
    EXPECT_EQ(0, result.tx_mcs.rx_diff());
    EXPECT_EQ(1, result.tx_mcs.max_ss());
    EXPECT_EQ(0, result.tx_mcs.ueqm());

    lhs.rx_mcs_head.set_bitmask(0xfff0fff);
    rhs.rx_mcs_head.set_bitmask(0x001fff0);
    result = IntersectMcs(lhs, rhs);
    EXPECT_EQ(0x0010ff0ul, result.rx_mcs_head.bitmask());
}

namespace wlan_mlme = ::fuchsia::wlan::mlme;
TEST(McsTest, ConvertFromFidl) {
    wlan_mlme::SupportedMcsSet fidl;
    fidl.rx_mcs_set = 0xf0f0f0f0f0f0f0f0;
    fidl.rx_highest_rate = 1023;
    fidl.tx_mcs_set_defined = true;
    fidl.tx_rx_diff = true;
    fidl.tx_max_ss = 2;
    fidl.tx_ueqm = true;

    auto mcs_set = SupportedMcsSetFromFidl(fidl);
    EXPECT_EQ(0xf0f0f0f0f0f0f0f0, mcs_set.rx_mcs_head.bitmask());
    EXPECT_EQ(1023, mcs_set.rx_mcs_tail.highest_rate());
    EXPECT_EQ(1, mcs_set.tx_mcs.set_defined());
    EXPECT_EQ(1, mcs_set.tx_mcs.rx_diff());
    EXPECT_EQ(2, mcs_set.tx_mcs.max_ss_human());
    EXPECT_EQ(1, mcs_set.tx_mcs.max_ss());
    EXPECT_EQ(1, mcs_set.tx_mcs.ueqm());
}

}  // namespace
}  // namespace wlan
