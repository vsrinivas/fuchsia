// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "macaddr.h"
#include <gtest/gtest.h>

namespace wlan {
namespace {

class MacAddrTest : public ::testing::Test {
   protected:
};

TEST_F(MacAddrTest, Some) {
    MacAddr zero_addr;

    zero_addr.Set(kZeroMac);
    EXPECT_EQ(0x00, zero_addr.byte[0]);
    EXPECT_EQ(true, zero_addr.IsZero());

    MacAddr bcast_addr;
    bcast_addr.Set(kBcastMac);
    EXPECT_EQ(0xff, bcast_addr.byte[0]);
    EXPECT_EQ(true, bcast_addr.IsBcast());
    EXPECT_EQ(true, bcast_addr.IsMcast());
    EXPECT_EQ(false, bcast_addr.IsZero());
    EXPECT_EQ(true, bcast_addr.IsLocalAdmin());
    EXPECT_EQ(true, bcast_addr.Gt(kZeroMac));
    EXPECT_EQ(false, bcast_addr.Lt(kZeroMac));
    EXPECT_EQ(false, bcast_addr.IsGroupAddr());

    MacAddr addr1;
    std::string addr1_in_str = "48:0f:cf:54:b9:b1";
    addr1.Set(addr1_in_str);

    MacAddr addr2;
    uint8_t addr2_in_value[kMacAddrLen] = {0x48, 0x0f, 0xcf, 0x54, 0xb9, 0xb1};
    addr2.Set(addr2_in_value);

    EXPECT_EQ(true, addr1.Equals(addr2));
    EXPECT_EQ(true, addr2.Equals(addr1));
    EXPECT_EQ(false, addr2.IsMcast());
    EXPECT_EQ(false, addr2.IsBcast());
    EXPECT_EQ(false, addr2.IsZero());
    EXPECT_EQ(true, addr2.Gt(kZeroMac));
    EXPECT_EQ(false, addr2.Lt(kZeroMac));
    EXPECT_EQ(false, addr2.Gt(kBcastMac));
    EXPECT_EQ(true, addr2.Lt(kBcastMac));
}

}  // namespace
}  // namespace wlan
