// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/buffer_reader.h>
#include <wlan/common/buffer_writer.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>

#include "test_utils.h"

namespace wlan {
namespace {

template<size_t N>
struct Container {
    uint8_t data[N];
};

TEST(BufferUtils, Writer) {
    uint8_t buf[16] = {};
    std::iota(buf, buf + sizeof(buf), 0);

    BufferWriter w(buf);
    auto c1 = w.Write<Container<4>>();
    c1->data[0] = 1;
    c1->data[1] = 0;
    c1->data[2] = 0;
    c1->data[3] = 2;

    auto c2 = w.Write<Container<2>>();
    c2->data[0] = 3;
    c2->data[1] = 4;

    uint8_t data[4] = {5, 6, 7, 8};
    w.Write(data);

    w.WriteByte(0xffu);

    EXPECT_EQ(w.WrittenBytes(), 11u);
    EXPECT_EQ(w.RemainingBytes(), 5u);
    const uint8_t expected[5] = { 11, 12, 13, 14, 15 };
    EXPECT_RANGES_EQ(expected, w.RemainingBuffer());

    EXPECT_EQ(buf[0], 1);
    EXPECT_EQ(buf[1], 0);
    EXPECT_EQ(buf[2], 0);
    EXPECT_EQ(buf[3], 2);
    EXPECT_EQ(buf[4], 3);
    EXPECT_EQ(buf[5], 4);
    EXPECT_EQ(buf[6], 5);
    EXPECT_EQ(buf[9], 8);
    EXPECT_EQ(buf[10], 0xffu);
}

TEST(BufferUtils, Reader) {
    uint8_t buf[16] = {};
    std::iota(buf, buf + sizeof(buf), 0);

    BufferReader r(buf);
    auto c1 = r.Read<Container<4>>();
    ASSERT_NE(c1, nullptr);
    EXPECT_EQ(c1->data[0], 0u);
    EXPECT_EQ(c1->data[3], 3u);

    auto c2 = r.ReadValue<Container<2>>();
    ASSERT_TRUE(c2);
    EXPECT_EQ(c2->data[0], 4u);
    EXPECT_EQ(c2->data[1], 5u);

    auto c3 = r.Peek<Container<3>>();
    ASSERT_NE(c3, nullptr);
    EXPECT_EQ(c3->data[0], 6u);
    EXPECT_EQ(c3->data[2], 8u);

    auto read_data = r.Read(3);
    ASSERT_FALSE(read_data.empty());
    ASSERT_EQ(read_data.size(), 3u);
    EXPECT_EQ(read_data[0], 6u);
    EXPECT_EQ(read_data[2], 8u);

    EXPECT_EQ(r.ReadBytes(), 9u);
    EXPECT_EQ(r.RemainingBytes(), 7u);

    // Read over the remaining buffer size.
    EXPECT_TRUE(r.Read(8).empty());
    EXPECT_EQ(r.Peek<Container<8>>(), nullptr);
    EXPECT_EQ(r.Read<Container<8>>(), nullptr);

    auto remaining = r.ReadRemaining();
    ASSERT_NE(remaining.data(), nullptr);
    ASSERT_EQ(remaining.size(), 7u);
    EXPECT_EQ(r.ReadBytes(), 16u);
    EXPECT_EQ(r.RemainingBytes(), 0u);
    EXPECT_EQ(remaining[0], 9u);
    EXPECT_EQ(remaining[6], 15u);
}

TEST(BufferUtils, Reader_ReadArray) {
    uint8_t buf[7] = { 0, 1, 2, 3, 4, 5, 6 };
    BufferReader r(buf);
    EXPECT_TRUE(r.ReadArray<uint16_t>(4).empty());

    auto span = r.ReadArray<uint16_t>(3);
    EXPECT_RANGES_EQ(std::vector<uint16_t>({ 0x0100u, 0x0302u, 0x0504u }), span);
    EXPECT_EQ(6u, r.ReadBytes());
}

}  // namespace
}  // namespace wlan
