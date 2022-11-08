// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../probe_sequence.h"

#include <gtest/gtest.h>

namespace wlan {
namespace {

TEST(ProbeSequenceTest, RandomSequence) {
  auto random_sequence = ProbeSequence::RandomSequence();
  std::set<tx_vec_idx_t> seen{};
  tx_vec_idx_t count = 0;
  ProbeSequence::Entry entry;
  tx_vec_idx_t picked;
  random_sequence.Next(&entry, &picked);
  for (uint8_t i = 0; i < ProbeSequence::kNumProbeSequece; ++i) {
    do {
      seen.emplace(picked);
      ++count;
    } while (!random_sequence.Next(&entry, &picked));
    EXPECT_EQ(ProbeSequence::kSequenceLength, seen.size());
    EXPECT_EQ(ProbeSequence::kSequenceLength, count);
    EXPECT_EQ(kStartIdx, *seen.cbegin());
    EXPECT_EQ(kMaxValidIdx, *(--seen.cend()));
    seen.clear();
    count = 0;
  }
}

}  // namespace
}  // namespace wlan
