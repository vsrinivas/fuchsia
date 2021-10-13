// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <limits>

#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

namespace {

TEST(AlgorithmTest, RoundUp) {
  EXPECT_EQ(fbl::round_up(0u, 1u), 0u);
  EXPECT_EQ(fbl::round_up(0u, 5u), 0u);
  EXPECT_EQ(fbl::round_up(5u, 5u), 5u);

  EXPECT_EQ(fbl::round_up(1u, 6u), 6u);
  EXPECT_EQ(fbl::round_up(6u, 1u), 6u);
  EXPECT_EQ(fbl::round_up(6u, 3u), 6u);
  EXPECT_EQ(fbl::round_up(6u, 4u), 8u);

  EXPECT_EQ(fbl::round_up(15u, 8u), 16u);
  EXPECT_EQ(fbl::round_up(16u, 8u), 16u);
  EXPECT_EQ(fbl::round_up(17u, 8u), 24u);
  EXPECT_EQ(fbl::round_up(123u, 100u), 200u);
  EXPECT_EQ(fbl::round_up(123456u, 1000u), 124000u);

  uint64_t large_int = std::numeric_limits<uint32_t>::max() + 1LLU;
  EXPECT_EQ(fbl::round_up(large_int, 64U), large_int);
  EXPECT_EQ(fbl::round_up(large_int, 64LLU), large_int);
  EXPECT_EQ(fbl::round_up(large_int + 63LLU, 64U), large_int + 64LLU);
  EXPECT_EQ(fbl::round_up(large_int + 63LLU, 64LLU), large_int + 64LLU);
  EXPECT_EQ(fbl::round_up(large_int, 3U), large_int + 2LLU);
  EXPECT_EQ(fbl::round_up(large_int, 3LLU), large_int + 2LLU);

  EXPECT_EQ(fbl::round_up(2U, large_int), large_int);
  EXPECT_EQ(fbl::round_up(2LLU, large_int), large_int);
}

TEST(AlgorithmTest, RoundDown) {
  EXPECT_EQ(fbl::round_down(0u, 1u), 0u);
  EXPECT_EQ(fbl::round_down(0u, 5u), 0u);
  EXPECT_EQ(fbl::round_down(5u, 5u), 5u);

  EXPECT_EQ(fbl::round_down(1u, 6u), 0u);
  EXPECT_EQ(fbl::round_down(6u, 1u), 6u);
  EXPECT_EQ(fbl::round_down(6u, 3u), 6u);
  EXPECT_EQ(fbl::round_down(6u, 4u), 4u);

  EXPECT_EQ(fbl::round_down(15u, 8u), 8u);
  EXPECT_EQ(fbl::round_down(16u, 8u), 16u);
  EXPECT_EQ(fbl::round_down(17u, 8u), 16u);
  EXPECT_EQ(fbl::round_down(123u, 100u), 100u);
  EXPECT_EQ(fbl::round_down(123456u, 1000u), 123000u);

  uint64_t large_int = std::numeric_limits<uint32_t>::max() + 1LLU;
  EXPECT_EQ(fbl::round_down(large_int, 64U), large_int);
  EXPECT_EQ(fbl::round_down(large_int, 64LLU), large_int);
  EXPECT_EQ(fbl::round_down(large_int + 63LLU, 64U), large_int);
  EXPECT_EQ(fbl::round_down(large_int + 63LLU, 64LLU), large_int);
  EXPECT_EQ(fbl::round_down(large_int + 65LLU, 64U), large_int + 64LLU);
  EXPECT_EQ(fbl::round_down(large_int + 65LLU, 64LLU), large_int + 64LLU);
  EXPECT_EQ(fbl::round_down(large_int + 2LLU, 3U), large_int + 2LLU);
  EXPECT_EQ(fbl::round_down(large_int + 2LLU, 3LLU), large_int + 2LLU);

  EXPECT_EQ(fbl::round_down(2U, large_int), 0);
  EXPECT_EQ(fbl::round_down(2LLU, large_int), 0);
}

}  // namespace
