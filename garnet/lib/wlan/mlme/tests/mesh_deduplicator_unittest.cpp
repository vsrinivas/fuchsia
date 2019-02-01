// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/mlme/mesh/deduplicator.h>

#include "test_utils.h"

namespace wlan {

TEST(DeDuplicator, HandleUniquePackets) {
    // set the cache size
    DeDuplicator dedup(5);

    // send unique packets
    for (uint64_t addr = 0; addr < 2; addr++) {
        for (uint32_t seq = 0; seq < 2; seq++) {
            common::MacAddr macAddr(addr);
            ASSERT_EQ(false, dedup.DeDuplicate(macAddr, seq));
        }
    }

    // send duplicate packets
    for (uint64_t addr = 0; addr < 2; addr++) {
        for (uint32_t seq = 0; seq < 2; seq++) {
            common::MacAddr macAddr(addr);
            ASSERT_EQ(true, dedup.DeDuplicate(macAddr, seq));
        }
    }

    // send unique packets again
    for (uint64_t addr = 10; addr < 20; addr++) {
        for (uint32_t seq = 0; seq < 5; seq++) {
            common::MacAddr macAddr(addr);
            ASSERT_EQ(false, dedup.DeDuplicate(macAddr, seq));
        }
    }
}

}  // namespace wlan
