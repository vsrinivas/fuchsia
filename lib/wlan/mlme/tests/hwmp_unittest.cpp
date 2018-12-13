// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <lib/timekeeper/test_clock.h>
#include <wlan/mlme/mesh/hwmp.h>

#include "test_timer.h"
#include "test_utils.h"

namespace wlan {

TEST(Hwmp, HwmpSeqnoLessThan) {
    EXPECT_TRUE(HwmpSeqnoLessThan(2, 5));
    EXPECT_FALSE(HwmpSeqnoLessThan(5, 2));

    EXPECT_FALSE(HwmpSeqnoLessThan(5, 5));

    // Edge case: numbers exactly 2^31 apart
    EXPECT_FALSE(HwmpSeqnoLessThan(5, 5 + (1u << 31)));
    EXPECT_FALSE(HwmpSeqnoLessThan(5 + (1u << 31), 5));

    // One step away from the edge case
    EXPECT_TRUE(HwmpSeqnoLessThan(6, 5 + (1u << 31)));
    EXPECT_FALSE(HwmpSeqnoLessThan(5 + (1u << 31), 6));

    // One step away from the edge case in the other direction
    EXPECT_FALSE(HwmpSeqnoLessThan(4, 5 + (1u << 31)));
    EXPECT_TRUE(HwmpSeqnoLessThan(5 + (1u << 31), 4));
}

TEST(Hwmp, HandlePreqAddressedToUs) {
    // clang-format off
    const uint8_t preq[] = {
        130, 37,
        0x00, // flags: no address extension
        0x03, // hop count
        0x20, // element ttl
        0x04, 0x05, 0x06, 0x07, // path discovery ID
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
        0x07, 0x00, 0x00, 0x00, // originator hwmp seqno
        0x05, 0x00, 0x00, 0x00, // lifetime: 5 TU = 5120 mircoseconds
        200, 0, 0, 0, // metric
        1, // target count
        // Target 1
        0x00, // target flags
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // target address
        0x09, 0x00, 0x00, 0x00, // target hwmp seqno
    };
    // clang-format on
    timekeeper::TestClock clock;
    clock.Set(zx::time(1000));
    HwmpState state(fbl::make_unique<TestTimer>(123, &clock));
    PathTable table;
    Sequence seq;

    const common::MacAddr self_addr("aa:aa:aa:aa:aa:aa");

    auto outgoing_packets = HandleHwmpAction(preq, common::MacAddr("11:11:11:11:11:11"), self_addr,
                                             100, MacHeaderWriter(self_addr, &seq), &state, &table);

    // 1. Expect an outgoing PREP frame
    {
        ASSERT_EQ(outgoing_packets.size(), 1u);
        auto packet = outgoing_packets.Dequeue();
        ASSERT_NE(packet, nullptr);

        // clang-format off
        const uint8_t expected_prep_frame[] = {
            // Mgmt header
            0xd0, 0x00, 0x00, 0x00, // fc, duration
            0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr2
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr3
            0x10, 0x00, // seq ctl
            // Action
            13, // category (mesh)
            1, // action = HWMP mesh path selection
            // Prep element
            131, 31,
            0x00, 0x00, 0x20, // flags, hop count, elem ttl
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // target addr
            0x0a, 0x00, 0x00, 0x00, // target hwmp seqno: should be advanced to incoming seqno + 1
            0x05, 0x00, 0x00, 0x00, // lifetime: preserved from preq
            0x0, 0x0, 0x0, 0x0, // metric
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
            0x07, 0x00, 0x00, 0x00, // originator hwmp seqno
        };
        // clang-format on
        EXPECT_RANGES_EQ(expected_prep_frame, Span<const uint8_t>(*packet));
    }

    // 2. Expect the path table to be updated with the path to the originator
    {
        auto orig_path = table.GetPath(common::MacAddr("08:09:0a:0b:0c:0d"));
        ASSERT_NE(orig_path, nullptr);
        EXPECT_EQ(common::MacAddr("11:11:11:11:11:11"), orig_path->next_hop);
        EXPECT_EQ(std::optional<uint32_t>(7), orig_path->hwmp_seqno);
        EXPECT_EQ(zx::time(1000 + 5 * 1024 * 1000), orig_path->expiration_time);
        EXPECT_EQ(100u + 200u, orig_path->metric);
        EXPECT_EQ(4u, orig_path->hop_count);
    }

    // 3. Expect the path table to be updated with the path to the transmitter
    {
        auto transmitter_path = table.GetPath(common::MacAddr("11:11:11:11:11:11"));
        ASSERT_NE(transmitter_path, nullptr);
        EXPECT_EQ(common::MacAddr("11:11:11:11:11:11"), transmitter_path->next_hop);
        EXPECT_EQ(std::optional<uint32_t>{}, transmitter_path->hwmp_seqno);
        EXPECT_EQ(zx::time(1000 + 5 * 1024 * 1000), transmitter_path->expiration_time);
        EXPECT_EQ(100u, transmitter_path->metric);
        EXPECT_EQ(1u, transmitter_path->hop_count);
    }

    // 4. Expect our sequence number to be updated
    EXPECT_EQ(10u, state.our_hwmp_seqno);
}

}  // namespace wlan