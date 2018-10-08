// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "../probe_sequence.h"

namespace wlan {
namespace {

TEST(ProbeSequenceTest, GenerateRandomProbeTable) {
    auto probe_sequence_table = ProbeSequence::RandomProbeTable();
    std::set<tx_vec_idx_t> seen{};
    EXPECT_EQ(ProbeSequence::kNumProbeSequece, probe_sequence_table.size());
    for (const auto& sequence : probe_sequence_table) {
        EXPECT_EQ(ProbeSequence::kSequenceLength, sequence.size());
        seen.clear();
        for (tx_vec_idx_t i : sequence) {
            seen.emplace(i);
        }
        EXPECT_EQ(ProbeSequence::kSequenceLength, seen.size());
        EXPECT_EQ(kStartIdx, *seen.cbegin());
        EXPECT_EQ(kMaxValidIdx, *(--seen.cend()));
    }
}

}  // namespace
}  // namespace wlan
