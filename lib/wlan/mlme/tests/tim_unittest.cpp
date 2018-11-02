// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <wlan/mlme/ap/tim.h>
#include <wlan/mlme/mac_frame.h>
#include <zircon/types.h>

namespace wlan {
namespace {

class TimTest : public ::testing::Test {
   protected:
    TrafficIndicationMap tim_;
};

TEST_F(TimTest, Initialization) {
    EXPECT_FALSE(tim_.HasDozingClients());
    EXPECT_FALSE(tim_.HasGroupTraffic());
}

TEST_F(TimTest, HasTraffic) {
    EXPECT_FALSE(tim_.HasDozingClients());
    EXPECT_FALSE(tim_.HasGroupTraffic());

    // Add and test unicast traffic.
    tim_.SetTrafficIndication(1, true);
    EXPECT_TRUE(tim_.HasDozingClients());
    tim_.SetTrafficIndication(4, true);
    tim_.SetTrafficIndication(9, true);
    tim_.SetTrafficIndication(35, true);
    EXPECT_TRUE(tim_.HasDozingClients());

    // Add and test multicast traffic.
    EXPECT_FALSE(tim_.HasGroupTraffic());
    tim_.SetTrafficIndication(0, true);
    EXPECT_TRUE(tim_.HasGroupTraffic());
}

TEST_F(TimTest, ClearTrafficIndication) {
    // Add unicast and multicast traffic and test for it.
    tim_.SetTrafficIndication(0, true);
    tim_.SetTrafficIndication(1, true);
    tim_.SetTrafficIndication(4, true);
    tim_.SetTrafficIndication(9, true);
    tim_.SetTrafficIndication(35, true);
    EXPECT_TRUE(tim_.HasDozingClients());
    EXPECT_TRUE(tim_.HasGroupTraffic());

    // Reset and test multicast traffic.
    tim_.SetTrafficIndication(0, false);
    EXPECT_FALSE(tim_.HasGroupTraffic());

    // Reset and test unicast traffic.
    tim_.SetTrafficIndication(1, false);
    EXPECT_TRUE(tim_.HasDozingClients());
    tim_.SetTrafficIndication(4, false);
    tim_.SetTrafficIndication(9, false);
    tim_.SetTrafficIndication(35, false);
    EXPECT_FALSE(tim_.HasDozingClients());
}

TEST_F(TimTest, ClearTraffic) {
    // Add and test unicast and multicast traffic.
    tim_.SetTrafficIndication(0, true);
    tim_.SetTrafficIndication(35, true);
    EXPECT_TRUE(tim_.HasDozingClients());
    EXPECT_TRUE(tim_.HasGroupTraffic());

    // Clear all traffic and test.
    tim_.Clear();
    EXPECT_FALSE(tim_.HasDozingClients());
    EXPECT_FALSE(tim_.HasGroupTraffic());
}

TEST_F(TimTest, WriteEmptyPartialVirtualBitmap) {
    uint8_t buf[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Write PVB to buffer.
    size_t bitmap_len;
    uint8_t bitmap_offset;
    auto status = tim_.WritePartialVirtualBitmap(buf, sizeof(buf), &bitmap_len, &bitmap_offset);

    // PVB should be 1 byte in size and empty since there is no traffic.
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(bitmap_len, 1u);
    EXPECT_EQ(bitmap_offset, 0u);
    EXPECT_EQ(buf[0], 0u);

    // Verify no other bytes but the first one were written.
    EXPECT_EQ(buf[1], 0xFF);
    EXPECT_EQ(buf[2], 0xFF);
    EXPECT_EQ(buf[3], 0xFF);
    EXPECT_EQ(buf[4], 0xFF);
}

TEST_F(TimTest, WriteNoOffsetPopulatedPartialVirtualBitmap) {
    uint8_t buf[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Add unicast traffic.
    tim_.SetTrafficIndication(1, true);
    tim_.SetTrafficIndication(4, true);
    tim_.SetTrafficIndication(9, true);
    tim_.SetTrafficIndication(35, true);

    // Write PVB to buffer.
    size_t bitmap_len;
    uint8_t bitmap_offset;
    auto status = tim_.WritePartialVirtualBitmap(buf, sizeof(buf), &bitmap_len, &bitmap_offset);

    // PVB should be 5 bytes in size due to the large spread of AIDs.
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(bitmap_len, 5u);
    EXPECT_EQ(bitmap_offset, 0u);

    // Test for correct content of buffer.
    EXPECT_EQ(buf[0], 18u);
    EXPECT_EQ(buf[1], 2u);
    EXPECT_EQ(buf[2], 0u);
    EXPECT_EQ(buf[3], 0u);
    EXPECT_EQ(buf[4], 8u);
}

TEST_F(TimTest, WriteOffsetPopulatedPartialVirtualBitmap) {
    uint8_t buf[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Add unicast traffic which allows to skip bytes at the beginning.
    tim_.SetTrafficIndication(35, true);
    tim_.SetTrafficIndication(48, true);

    // Write PVB to buffer.
    size_t bitmap_len;
    uint8_t bitmap_offset;
    auto status = tim_.WritePartialVirtualBitmap(buf, sizeof(buf), &bitmap_len, &bitmap_offset);

    // PVB should be 3 bytes in size and yield an offset.
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(bitmap_len, 3u);
    EXPECT_EQ(bitmap_offset, 2u);

    // Test for correct content of buffer.
    EXPECT_EQ(buf[0], 8u);
    EXPECT_EQ(buf[1], 0u);
    EXPECT_EQ(buf[2], 1u);
    EXPECT_EQ(buf[3], 0xFF);
    EXPECT_EQ(buf[4], 0xFF);
}

TEST_F(TimTest, WriteChangingPartialVirtualBitmap) {
    uint8_t buf[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Add unicast traffic which allows to skip bytes at the beginning.
    tim_.SetTrafficIndication(35, true);
    tim_.SetTrafficIndication(48, true);

    // Write PVB to buffer.
    size_t bitmap_len;
    uint8_t bitmap_offset;
    auto status = tim_.WritePartialVirtualBitmap(buf, sizeof(buf), &bitmap_len, &bitmap_offset);

    // PVB should be 3 bytes in size and yield an offset.
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(bitmap_len, 3u);
    EXPECT_EQ(bitmap_offset, 2u);

    // Test for correct content of buffer.
    EXPECT_EQ(buf[0], 8u);
    EXPECT_EQ(buf[1], 0u);
    EXPECT_EQ(buf[2], 1u);
    EXPECT_EQ(buf[3], 0xFF);
    EXPECT_EQ(buf[4], 0xFF);

    // Clear one uicast traffic and clear buffer at corresponding location.
    // This allows to test whether or not writing PVB respects the change
    // correctly.
    tim_.SetTrafficIndication(48, false);
    buf[1] = 0xFF;
    buf[2] = 0xFF;

    // Write PVB to buffer.
    status = tim_.WritePartialVirtualBitmap(buf, sizeof(buf), &bitmap_len, &bitmap_offset);

    // PVB should now only be 1 byte in size but keep the same offset.
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(bitmap_len, 1u);
    EXPECT_EQ(bitmap_offset, 2u);

    // Test for correct content of buffer. Only first byte should have been written.
    EXPECT_EQ(buf[0], 8u);
    EXPECT_EQ(buf[1], 0xFF);
    EXPECT_EQ(buf[2], 0xFF);
    EXPECT_EQ(buf[3], 0xFF);
    EXPECT_EQ(buf[4], 0xFF);

    // Add some more unicast traffic which will extend the PVB and removes the offset.
    tim_.SetTrafficIndication(1, true);
    tim_.SetTrafficIndication(4, true);
    tim_.SetTrafficIndication(9, true);

    // Write PVB to buffer.
    status = tim_.WritePartialVirtualBitmap(buf, sizeof(buf), &bitmap_len, &bitmap_offset);

    // PVB should now be 5 bytes in size and have no offset.
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(bitmap_len, 5u);
    EXPECT_EQ(bitmap_offset, 0u);

    // Test for correct content of buffer.
    EXPECT_EQ(buf[0], 18u);
    EXPECT_EQ(buf[1], 2u);
    EXPECT_EQ(buf[2], 0u);
    EXPECT_EQ(buf[3], 0u);
    EXPECT_EQ(buf[4], 8u);
}

TEST_F(TimTest, WriteLastBytePartialVirtualBitmap) {
    uint8_t buf[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Add unicast traffic for last AIDs.
    tim_.SetTrafficIndication(2005, true);
    tim_.SetTrafficIndication(2006, true);
    tim_.SetTrafficIndication(2007, true);

    // Write PVB to buffer.
    size_t bitmap_len;
    uint8_t bitmap_offset;
    auto status = tim_.WritePartialVirtualBitmap(buf, sizeof(buf), &bitmap_len, &bitmap_offset);

    // PVB should be 1 byte in size and have an offset of 125 bytes (largest possible offset).
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(bitmap_len, 1u);
    EXPECT_EQ(bitmap_offset, 125u);

    // Test for correct content in buffer.
    EXPECT_EQ(buf[0], 224u);
    EXPECT_EQ(buf[1], 0xFF);
    EXPECT_EQ(buf[2], 0xFF);
    EXPECT_EQ(buf[3], 0xFF);
    EXPECT_EQ(buf[4], 0xFF);
}

TEST_F(TimTest, WriteMaxSizedPartialVirtualBitmap) {
    constexpr size_t kMaxBitmapLen = kMaxTimBitmapLen;

    uint8_t buf[kMaxBitmapLen];

    // Add unicast traffic for lowest and highest AID which won't allow offsets.
    tim_.SetTrafficIndication(1, true);
    tim_.SetTrafficIndication(2007, true);

    // Write PVB to buffer.
    size_t bitmap_len;
    uint8_t bitmap_offset;
    auto status = tim_.WritePartialVirtualBitmap(buf, sizeof(buf), &bitmap_len, &bitmap_offset);

    // PVB should be 251 bytes in size (maximum size) and have no offset.
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(bitmap_len, kMaxBitmapLen);
    EXPECT_EQ(bitmap_offset, 0u);

    // Test first byte of buffer for correct content.
    EXPECT_EQ(buf[0], 2u);

    // Test buffer content inbetween AIDs for correct content.
    for (size_t i = 1; i < kMaxBitmapLen - 1; i++) {
        EXPECT_EQ(buf[i], 0);
    }

    // Test last byte of buffer for correct content.
    EXPECT_EQ(buf[kMaxBitmapLen - 1], 128u);
}

TEST_F(TimTest, IgnoreGroupTrafficInPartialVirtualBitmap) {
    uint8_t buf[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Add unicast and multicast traffic.
    tim_.SetTrafficIndication(0, true);
    tim_.SetTrafficIndication(35, true);
    tim_.SetTrafficIndication(48, true);

    // Write PVB to buffer.
    size_t bitmap_len;
    uint8_t bitmap_offset;
    auto status = tim_.WritePartialVirtualBitmap(buf, sizeof(buf), &bitmap_len, &bitmap_offset);

    // PVB should be 3 bytes in size and have an offset and thus effectively
    // ignore the group traffic of AID 0.
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(bitmap_len, 3u);
    EXPECT_EQ(bitmap_offset, 2u);

    // Test for correct content in buffer.
    EXPECT_EQ(buf[0], 8u);
    EXPECT_EQ(buf[1], 0u);
    EXPECT_EQ(buf[2], 1u);
    EXPECT_EQ(buf[3], 0xFF);
    EXPECT_EQ(buf[4], 0xFF);
}

}  // namespace
}  // namespace wlan
