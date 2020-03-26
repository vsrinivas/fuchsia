// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/bit_ops.h"

#include <gtest/gtest.h>

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

TEST(BitOps, CountOnes) {
  EXPECT_EQ(0u, CountOnes(0u));
  EXPECT_EQ(1u, CountOnes(1u));
  EXPECT_EQ(4u, CountOnes(0xF0000000u));
  EXPECT_EQ(8u, CountOnes(0xF000F000u));
  EXPECT_EQ(8u, CountOnes(0x000F000Fu));
  EXPECT_EQ(24u, CountOnes(0xEEEEEEEEu));
}

template <typename T>
void TestSetBitsAtAndAboveIndex() {
  size_t kNumBits = sizeof(T) * 8;

  const T kZeros = 0u;
  const T kOnes = ~kZeros;

  for (size_t i = 0; i < kNumBits; ++i) {
    const T kAtAndAboveBits = kOnes << i;
    const T kBelowBits = ~kAtAndAboveBits;

    T bits = kZeros;
    SetBitsAtAndAboveIndex(&bits, i);
    EXPECT_EQ(bits & kAtAndAboveBits, kAtAndAboveBits);
    EXPECT_EQ(bits & kBelowBits, kZeros);

    bits = kOnes;
    SetBitsAtAndAboveIndex(&bits, i);
    EXPECT_EQ(bits, kOnes);
  }
}

TEST(BitOps, SetBitsAtAndAboveIndex) {
  TestSetBitsAtAndAboveIndex<uint16_t>();
  TestSetBitsAtAndAboveIndex<uint32_t>();
  TestSetBitsAtAndAboveIndex<uint64_t>();
  TestSetBitsAtAndAboveIndex<int16_t>();
  TestSetBitsAtAndAboveIndex<int32_t>();
  TestSetBitsAtAndAboveIndex<int64_t>();
}

template <typename T>
void TestRotateLeft() {
  EXPECT_EQ(2U, RotateLeft(1U, 1));
  EXPECT_EQ(4U, RotateLeft(1U, 2));
  EXPECT_EQ(6U, RotateLeft(3U, 1));
  EXPECT_EQ(12U, RotateLeft(3U, 2));

  const T max = std::numeric_limits<T>::max();
  const int digits = std::numeric_limits<T>::digits;

  for (int i = 1; i < digits; ++i) {
    EXPECT_EQ(max, RotateLeft(max, i));
  }

  const T high_order_bit = T(1) << digits - 1;
  EXPECT_EQ(1U, RotateLeft(high_order_bit, 1));
}

TEST(BitOps, RotateLeft) {
  TestRotateLeft<uint8_t>();
  TestRotateLeft<uint16_t>();
  TestRotateLeft<uint32_t>();
  TestRotateLeft<uint64_t>();
}

}  // namespace
