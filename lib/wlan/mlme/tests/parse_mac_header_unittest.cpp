// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/parse_mac_header.h>
#include <gtest/gtest.h>

#include "test_utils.h"

namespace wlan {
namespace common {

TEST(ParseDataFrameHeader, Minimal) {
    const uint8_t data[] = {
        0x08, 0x00, // fc: non-qos data, 3-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
    };
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(0u, r.RemainingBytes());
    EXPECT_EQ(data, reinterpret_cast<const uint8_t*>(parsed->fixed));
    EXPECT_EQ(MacAddr("11:11:11:11:11:11"), parsed->fixed->addr1);
    EXPECT_EQ(nullptr, parsed->addr4);
    EXPECT_EQ(nullptr, parsed->qos_ctrl);
    EXPECT_EQ(nullptr, parsed->ht_ctrl);
}

TEST(ParseDataFrameHeader, Full) {
    const uint8_t data[] = {
        0x88, 0x83, // fc: non-qos data, 4-address, ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, // addr4
        0x55, 0x66, // qos ctl
        0x77, 0x88, 0x99, 0xaa, // ht ctl
    };
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(0u, r.RemainingBytes());
    EXPECT_EQ(data, reinterpret_cast<const uint8_t*>(parsed->fixed));
    EXPECT_EQ(MacAddr("11:11:11:11:11:11"), parsed->fixed->addr1);

    ASSERT_NE(nullptr, parsed->addr4);
    EXPECT_EQ(MacAddr("44:44:44:44:44:44"), *parsed->addr4);

    ASSERT_NE(nullptr, parsed->qos_ctrl);
    EXPECT_EQ(0x6655u, parsed->qos_ctrl->val());

    ASSERT_NE(nullptr, parsed->ht_ctrl);
    EXPECT_EQ(0xaa998877u, parsed->ht_ctrl->val());
}

TEST(ParseDataFrameHeader, FixedPartTooShort) {
    const uint8_t data[] = {
        0x08, 0x00, // fc: non-qos data, 3-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, // one byte missing seq ctl
    };
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseDataFrameHeader, Addr4TooShort) {
    const uint8_t data[] = {
        0x88, 0x83, // fc: non-qos data, 4-address, ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x44, 0x44, 0x44, 0x44, 0x44, // one byte missing from addr4
    };
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseDataFrameHeader, QosControlTooShort) {
    const uint8_t data[] = {
        0x88, 0x83, // fc: non-qos data, 4-address, ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, // addr4
        0x55, // one byte missing from qos ctl
    };
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseDataFrameHeader, HtControlTooShort) {
    const uint8_t data[] = {
        0x88, 0x83, // fc: non-qos data, 4-address, ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, // addr4
        0x55, 0x66, // qos ctl
        0x77, 0x88, 0x99, // one byte missing from ht ctl
    };
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_FALSE(parsed);
}

} // namespace common
} // namespace wlan
