// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/util/bit_ops.h"

#include "gtest/gtest.h"

namespace {
using namespace escher;

TEST(BitOps, CountTrailingZeros) {
  // Some easy ones.
  ASSERT_EQ(0, CountTrailingZeros(0x1));
  ASSERT_EQ(1, CountTrailingZeros(0x2));
  ASSERT_EQ(31, CountTrailingZeros(0x80000000));

  // Iterate through additional cases.
  const uint32_t kOne = 1;
  const uint32_t kAlmostMax = ~0U;
  for (size_t i = 0; i < 32; ++i) {
    EXPECT_EQ(int32_t(i), CountTrailingZeros(kOne << i));
    EXPECT_EQ(int32_t(i), CountTrailingZeros(kAlmostMax << i));
  }
}

TEST(BitOps, CountLeadingZeros) {
  // Some easy ones.
  ASSERT_EQ(0, CountLeadingZeros(0x80000000));
  ASSERT_EQ(1, CountLeadingZeros(0x40000000));
  ASSERT_EQ(31, CountLeadingZeros(0x1));

  // Iterate through additional cases.
  const uint32_t kHighestBit = 0x80000000;
  const uint32_t kAlmostMax = ~0U;
  for (size_t i = 0; i < 32; ++i) {
    EXPECT_EQ(int32_t(i), CountLeadingZeros(kHighestBit >> i));
    EXPECT_EQ(int32_t(i), CountLeadingZeros(kAlmostMax >> i));
  }
}

}  // namespace
