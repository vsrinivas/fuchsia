// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/perr_destination_parser.h>

#include <gtest/gtest.h>

namespace wlan {
namespace common {

TEST(PerrDestinationParser, Empty) {
    PerrDestinationParser parser({});
    EXPECT_FALSE(parser.Next().has_value());
    EXPECT_FALSE(parser.ExtraBytesLeft());
}

TEST(PerrDestinationParser, TwoDestinations) {
    // clang-format off
    const uint8_t bytes[] = {
        // Target 1
        0x40, // flags: address extension
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, // dest addr
        0x11, 0x22, 0x33, 0x44, // HWMP seqno
        0x1a, 0x2a, 0x3a, 0x4a, 0x5a, 0x6a,  // ext addr
        0x55, 0x66, // reason code
        // Target 2
        0, // flags
        0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, // dest addr
        0x77, 0x88, 0x99, 0xaa, // HWMP seqno
        0xbb, 0xcc, // reason code
    };
    // clang-format on
    PerrDestinationParser parser(bytes);
    EXPECT_TRUE(parser.ExtraBytesLeft());
    {
        auto d = parser.Next();
        ASSERT_TRUE(d.has_value());
        EXPECT_EQ(0x44332211u, d->header->hwmp_seqno);
        ASSERT_NE(nullptr, d->ext_addr);
        EXPECT_EQ(MacAddr("1a:2a:3a:4a:5a:6a"), *d->ext_addr);
        EXPECT_EQ(0x6655u, d->tail->reason_code);
    }
    EXPECT_TRUE(parser.ExtraBytesLeft());
    {
        auto d = parser.Next();
        ASSERT_TRUE(d.has_value());
        EXPECT_EQ(0xaa998877u, d->header->hwmp_seqno);
        EXPECT_EQ(nullptr, d->ext_addr);
        EXPECT_EQ(0xccbbu, d->tail->reason_code);
    }
    EXPECT_FALSE(parser.Next().has_value());
    EXPECT_FALSE(parser.ExtraBytesLeft());
}

TEST(PerrDestinationParser, TooShortForHeader) {
    // clang-format off
    const uint8_t bytes[] = {
        0x00, // flags: no address extension
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, // dest addr
        0x11, 0x22, 0x33, // one byte missing from HWMP seqno
    };
    // clang-format on
    PerrDestinationParser parser(bytes);
    EXPECT_FALSE(parser.Next().has_value());
    EXPECT_TRUE(parser.ExtraBytesLeft());
}

TEST(PerrDestinationParser, TooShortForExtAddr) {
    // clang-format off
    const uint8_t bytes[] = {
        // Target 1
        0x40, // flags: address extension
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, // dest addr
        0x11, 0x22, 0x33, 0x44, // HWMP seqno
        0x1a, 0x2a, 0x3a, 0x4a, 0x5a, // one byte missing from ext addr
    };
    // clang-format on
    PerrDestinationParser parser(bytes);
    EXPECT_FALSE(parser.Next().has_value());
    EXPECT_TRUE(parser.ExtraBytesLeft());
}

TEST(PerrDestinationParser, TooShortForTail) {
    // clang-format off
    const uint8_t bytes[] = {
        // Target 1
        0x40, // flags: address extension
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, // dest addr
        0x11, 0x22, 0x33, 0x44, // HWMP seqno
        0x1a, 0x2a, 0x3a, 0x4a, 0x5a, 0x6a,  // ext addr
        0x55, // one byte missing from the reason code
    };
    // clang-format on
    PerrDestinationParser parser(bytes);
    EXPECT_FALSE(parser.Next().has_value());
    EXPECT_TRUE(parser.ExtraBytesLeft());
}

}  // namespace common
}  // namespace wlan
