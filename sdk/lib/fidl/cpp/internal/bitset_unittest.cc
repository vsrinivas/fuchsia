// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/bitset.h"

#include <zxtest/zxtest.h>

namespace fidl {
namespace internal {
namespace {

template <size_t Min, size_t Max>
struct RangeChecker {
  static_assert(Max >= Min);

  template <size_t N>
  static void AllTrue(const BitSet<N>& bitset) {
    EXPECT_TRUE(bitset.template IsSet<Max>());
    if constexpr (Min != Max) {
      RangeChecker<Min, Max - 1>::AllTrue(bitset);
    }
  }

  template <size_t N>
  static void AllFalse(const BitSet<N>& bitset) {
    EXPECT_FALSE(bitset.template IsSet<Max>());
    if constexpr (Min != Max) {
      RangeChecker<Min, Max - 1>::AllFalse(bitset);
    }
  }
};

TEST(BitSet, HasExpectedSize) {
  static_assert(sizeof(BitSet<0>) == 1);  // Empty struct specialization.
  static_assert(sizeof(BitSet<1>) == 8);
  static_assert(sizeof(BitSet<3>) == 8);
  static_assert(sizeof(BitSet<64>) == 8);
  static_assert(sizeof(BitSet<65>) == 16);
  static_assert(sizeof(BitSet<79>) == 16);
  static_assert(sizeof(BitSet<128>) == 16);
  static_assert(sizeof(BitSet<129>) == 24);
}

TEST(BitSet, ZeroBitSet) {
  BitSet<0> bitset;
  EXPECT_TRUE(bitset.IsEmpty());
  EXPECT_EQ(bitset.MaxSetIndex(), -1);

  BitSet<0> second = bitset;
  EXPECT_TRUE(second.IsEmpty());
  EXPECT_EQ(second.MaxSetIndex(), -1);
}

TEST(BitSet, EmptyBitSet) {
  BitSet<100> bitset;
  RangeChecker<0, 99>::AllFalse(bitset);
  EXPECT_TRUE(bitset.IsEmpty());
  EXPECT_EQ(bitset.MaxSetIndex(), -1);
}

TEST(BitSet, SetClearIsSet) {
  BitSet<200> bitset;
  RangeChecker<0, 199>::AllFalse(bitset);

  bitset.Set<0>();
  bitset.Set<20>();
  bitset.Set<160>();
  bitset.Set<199>();
  EXPECT_TRUE(bitset.IsSet<0>());
  RangeChecker<1, 19>::AllFalse(bitset);
  EXPECT_TRUE(bitset.IsSet<20>());
  RangeChecker<21, 159>::AllFalse(bitset);
  EXPECT_TRUE(bitset.IsSet<160>());
  RangeChecker<161, 198>::AllFalse(bitset);
  EXPECT_TRUE(bitset.IsSet<199>());

  bitset.Clear<0>();
  bitset.Clear<160>();
  bitset.Clear<199>();
  RangeChecker<0, 19>::AllFalse(bitset);
  EXPECT_TRUE(bitset.IsSet<20>());
  RangeChecker<21, 199>::AllFalse(bitset);
}

TEST(BitSet, IsEmpty) {
  BitSet<200> bitset;
  EXPECT_TRUE(bitset.IsEmpty());

  bitset.Set<199>();
  EXPECT_FALSE(bitset.IsEmpty());
  bitset.Clear<199>();
  EXPECT_TRUE(bitset.IsEmpty());

  bitset.Set<0>();
  EXPECT_FALSE(bitset.IsEmpty());
  bitset.Clear<0>();
  EXPECT_TRUE(bitset.IsEmpty());

  bitset.Set<121>();
  EXPECT_FALSE(bitset.IsEmpty());
  bitset.Clear<121>();
  EXPECT_TRUE(bitset.IsEmpty());
}

TEST(BitSet, MaxSetIndex) {
  BitSet<200> bitset;
  EXPECT_EQ(bitset.MaxSetIndex(), -1);

  bitset.Set<0>();
  EXPECT_EQ(bitset.MaxSetIndex(), 0);

  bitset.Set<3>();
  EXPECT_EQ(bitset.MaxSetIndex(), 3);

  bitset.Set<63>();
  EXPECT_EQ(bitset.MaxSetIndex(), 63);

  bitset.Set<64>();
  EXPECT_EQ(bitset.MaxSetIndex(), 64);

  bitset.Set<113>();
  EXPECT_EQ(bitset.MaxSetIndex(), 113);

  bitset.Set<199>();
  EXPECT_EQ(bitset.MaxSetIndex(), 199);

  bitset.Clear<199>();
  EXPECT_EQ(bitset.MaxSetIndex(), 113);
}

TEST(BitSet, CopyConstructor) {
  BitSet<200> bitset;
  bitset.Set<79>();

  RangeChecker<1, 78>::AllFalse(bitset);
  EXPECT_TRUE(bitset.IsSet<79>());
  RangeChecker<80, 199>::AllFalse(bitset);

  BitSet<200> other = bitset;

  RangeChecker<1, 78>::AllFalse(other);
  EXPECT_TRUE(other.IsSet<79>());
  RangeChecker<80, 199>::AllFalse(other);

  // Setting a bit in one shouldn't effect the other.
  other.Set<21>();
  EXPECT_FALSE(bitset.IsSet<21>());
  bitset.Set<20>();
  EXPECT_FALSE(other.IsSet<20>());
}

TEST(BitSet, CopyAssignment) {
  BitSet<200> bitset;
  bitset.Set<79>();

  RangeChecker<1, 78>::AllFalse(bitset);
  EXPECT_TRUE(bitset.IsSet<79>());
  RangeChecker<80, 199>::AllFalse(bitset);

  BitSet<200> other;
  other = bitset;

  RangeChecker<1, 78>::AllFalse(other);
  EXPECT_TRUE(other.IsSet<79>());
  RangeChecker<80, 199>::AllFalse(other);

  // Setting a bit in one shouldn't effect the other.
  other.Set<21>();
  EXPECT_FALSE(bitset.IsSet<21>());
  bitset.Set<20>();
  EXPECT_FALSE(other.IsSet<20>());
}

}  // namespace
}  // namespace internal
}  // namespace fidl
