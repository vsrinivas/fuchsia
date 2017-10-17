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
    EXPECT_EQ(true, bcast_addr > kZeroMac);
    EXPECT_EQ(false, bcast_addr < kZeroMac);
    EXPECT_EQ(false, bcast_addr.IsGroupAddr());

    MacAddr addr2({0x48, 0x0f, 0xcf, 0x54, 0xb9, 0xb1});
    EXPECT_EQ(false, addr2.IsMcast());
    EXPECT_EQ(false, addr2.IsBcast());
    EXPECT_EQ(false, addr2.IsZero());
    EXPECT_EQ(true, addr2 > kZeroMac);
    EXPECT_EQ(false, addr2 < kZeroMac);
    EXPECT_EQ(false, addr2 > kBcastMac);
    EXPECT_EQ(true, addr2 < kBcastMac);
}

TEST_F(MacAddrTest, Constructors) {
    uint8_t arr[kMacAddrLen] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MacAddr addr1(arr);
    MacAddr addr2;
    addr2.Set(arr);

    std::string str = "01:02:03:04:05:06";
    MacAddr addr3(str);
    MacAddr addr4;
    addr4.Set(str);

    MacAddr addr5({0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    MacAddr addr6("01:02:03:04:05:06");

    MacAddr addr7(addr6);

    EXPECT_EQ(true, addr1 == addr2);
    EXPECT_EQ(false, addr1 != addr2);
    EXPECT_EQ(true, addr2 == addr3);
    EXPECT_EQ(true, addr3 == addr4);
    EXPECT_EQ(true, addr4 == addr5);
    EXPECT_EQ(true, addr5 == addr6);
    EXPECT_EQ(true, addr6 == addr7);
    EXPECT_EQ(true, addr7 == addr1);
}

}  // namespace
}  // namespace wlan
