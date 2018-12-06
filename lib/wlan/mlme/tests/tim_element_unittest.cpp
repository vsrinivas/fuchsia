// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/tim_element.h>
#include <gtest/gtest.h>

namespace wlan {
namespace common {

static TimHeader tim_header(uint8_t offset) {
    TimHeader hdr;
    hdr.dtim_count = 1;
    hdr.dtim_period = 2;
    hdr.bmp_ctrl.set_group_traffic_ind(0);
    hdr.bmp_ctrl.set_offset(offset);
    return hdr;
}

TEST(TimElement, IsTrafficBuffered) {
    const uint8_t bitmap[] = { 0x12 };

    EXPECT_FALSE(IsTrafficBuffered(0,   tim_header(0), bitmap));
    EXPECT_TRUE( IsTrafficBuffered(1,   tim_header(0), bitmap));
    EXPECT_FALSE(IsTrafficBuffered(2,   tim_header(0), bitmap));
    EXPECT_FALSE(IsTrafficBuffered(3,   tim_header(0), bitmap));
    EXPECT_TRUE( IsTrafficBuffered(4,   tim_header(0), bitmap));
    EXPECT_FALSE(IsTrafficBuffered(5,   tim_header(0), bitmap));
    EXPECT_FALSE(IsTrafficBuffered(100, tim_header(0), bitmap));

    // Offset of 1 means "skip 16 bits"
    EXPECT_FALSE(IsTrafficBuffered(15,  tim_header(1), bitmap));
    EXPECT_FALSE(IsTrafficBuffered(16,  tim_header(1), bitmap));
    EXPECT_TRUE( IsTrafficBuffered(17,  tim_header(1), bitmap));
    EXPECT_FALSE(IsTrafficBuffered(18,  tim_header(1), bitmap));
    EXPECT_FALSE(IsTrafficBuffered(19,  tim_header(1), bitmap));
    EXPECT_TRUE( IsTrafficBuffered(20,  tim_header(1), bitmap));
    EXPECT_FALSE(IsTrafficBuffered(21,  tim_header(1), bitmap));
    EXPECT_FALSE(IsTrafficBuffered(22,  tim_header(1), bitmap));
    EXPECT_FALSE(IsTrafficBuffered(100, tim_header(1), bitmap));
}

TEST(TimElement, FindAndParseOk) {
    // Present & valid TIM
    std::vector<uint8_t> buf({ 0, 3, 'f', 'o', 'o', // SSID
                               5, 5, 1, 2, 3, 10, 20, // TIM
                               7, 3, 'A', 'B', 'C' }); // Country
    auto tim = FindAndParseTim(buf);
    ASSERT_TRUE(tim);
    EXPECT_EQ(tim->header.dtim_count, 1);
    EXPECT_EQ(tim->header.dtim_period, 2);
    EXPECT_EQ(tim->header.bmp_ctrl.val(), 3);
    ASSERT_EQ(tim->bitmap.size(), 2u);
    ASSERT_EQ(tim->bitmap[0], 10);
    ASSERT_EQ(tim->bitmap[1], 20);
}

TEST(TimElement, FindAndParseAbsent) {
    auto tim = FindAndParseTim(std::vector<uint8_t>({ 0, 3, 'f', 'o', 'o',
                                                      7, 3, 'A', 'B', 'C' }));
    ASSERT_FALSE(tim);
}

TEST(TimElement, FindAndParseInvalid) {
    auto tim = FindAndParseTim(std::vector<uint8_t>({ 0, 3, 'f', 'o', 'o',
                                                      5, 2, 1, 2,
                                                      7, 3, 'A', 'B', 'C' }));
    ASSERT_FALSE(tim);
}

} // namespace common
} // namespace wlan
