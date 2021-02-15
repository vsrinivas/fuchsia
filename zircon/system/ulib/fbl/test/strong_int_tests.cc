// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <type_traits>

#include <fbl/strong_int.h>
#include <zxtest/zxtest.h>

namespace {

TEST(StrongIntTest, TwoUint64DontConvert) {
  DEFINE_STRONG_INT(CpuCount, uint64_t);
  DEFINE_STRONG_INT(MemoryBytes, uint64_t);
  static_assert(!std::is_same<CpuCount, MemoryBytes>::value);
}

TEST(StrongIntTest, TwoUintsOfDifferentSizeDontConvert) {
  DEFINE_STRONG_INT(CpuCount, uint32_t);
  DEFINE_STRONG_INT(MemoryBytes, uint64_t);
  static_assert(!std::is_same<CpuCount, MemoryBytes>::value);
}

TEST(StrongIntTest, SameTypesWork) {
  DEFINE_STRONG_INT(Kilometers, uint32_t);

  Kilometers near(1), nearer(1);
  Kilometers far(2);
  ASSERT_EQ(near, nearer);
  ASSERT_NE(near, far);
  ASSERT_NE(near.value(), far.value());
  static_assert(Kilometers(1) < Kilometers(2));
  ASSERT_LT(near, far);
  near = far;
  ASSERT_EQ(near, far);
  std::swap(near, far);
  ASSERT_EQ(near, far);
}

TEST(StrongIntTest, ArithmaticOperations) {
  DEFINE_STRONG_INT(Strong, uint32_t);

  // Binary operators.
  static_assert((Strong(1) + Strong(2)) == Strong(3));
  static_assert((Strong(3) - Strong(1)) == Strong(2));
  static_assert((Strong(6) & Strong(2)) == Strong(2));
  static_assert((Strong(1) | Strong(2)) == Strong(3));
  static_assert((Strong(1) ^ Strong(2)) == Strong(3));

  static_assert((Strong(6) / 3) == Strong(2));
  static_assert((Strong(6) / Strong(3)) == 2);

  static_assert((Strong(2) * 3) == Strong(6));
  static_assert(3 * (Strong(2)) == Strong(6));

  static_assert((Strong(3) % 2) == Strong(1));
  static_assert((Strong(3) % Strong(2)) == Strong(1));

  static_assert((Strong(1) << 2) == Strong(4));
  static_assert((Strong(4) >> 1) == Strong(2));

  // Unary operators.
  static_assert(~Strong(0) == Strong(0xffffffff));
  static_assert(+Strong(6) == Strong(6));
  static_assert(-Strong(6) == Strong(-6));

  // explicit operator bool()
  if (Strong(0)) {
    FAIL();
  }
  if (!Strong(1)) {
    FAIL();
  }
  if (!Strong(100)) {
    FAIL();
  }
  if (Strong(1) && Strong(0)) {
    FAIL();
  }
  if (!Strong(1) || Strong(0)) {
    FAIL();
  }

  // Increment / decrement.
  {
    Strong s{1};
    EXPECT_EQ(s++, Strong{1});
    EXPECT_EQ(s, Strong{2});
  }
  {
    Strong s{1};
    EXPECT_EQ(++s, Strong{2});
    EXPECT_EQ(s, Strong{2});
  }
  {
    Strong s{1};
    EXPECT_EQ(s--, Strong{1});
    EXPECT_EQ(s, Strong{0});
  }
  {
    Strong s{1};
    EXPECT_EQ(--s, Strong{0});
    EXPECT_EQ(s, Strong{0});
  }

  // Update operators.
  // clang-format off
  { Strong s{1}; s  += Strong{2}; EXPECT_EQ(s, Strong(3)); }
  { Strong s{2}; s  -= Strong{1}; EXPECT_EQ(s, Strong(1)); }
  { Strong s{2}; s  &= Strong{3}; EXPECT_EQ(s, Strong(2)); }
  { Strong s{2}; s  |= Strong{1}; EXPECT_EQ(s, Strong(3)); }
  { Strong s{2}; s  ^= Strong{3}; EXPECT_EQ(s, Strong(1)); }
  { Strong s{6}; s  /= 2; EXPECT_EQ(s, Strong(3)); }
  { Strong s{2}; s  *= 3; EXPECT_EQ(s, Strong(6)); }
  { Strong s{6}; s  %= Strong{5}; EXPECT_EQ(s, Strong(1)); }
  { Strong s{4}; s >>= 1; EXPECT_EQ(s, Strong(2)); }
  { Strong s{1}; s <<= 1; EXPECT_EQ(s, Strong(2)); }
  // clang-format on

  // Update operators with plain RHS.
  // clang-format off
  { Strong s{6}; s  /= 2; EXPECT_EQ(s, Strong(3)); }
  { Strong s{2}; s  *= 3; EXPECT_EQ(s, Strong(6)); }
  { Strong s{6}; s  %= 5; EXPECT_EQ(s, Strong(1)); }
  { Strong s{4}; s >>= 1; EXPECT_EQ(s, Strong(2)); }
  { Strong s{1}; s <<= 1; EXPECT_EQ(s, Strong(2)); }
  // clang-format on
}

TEST(StrongIntTest, ChainedOps) {
  DEFINE_STRONG_INT(Strong, uint32_t);

  // Multiple arithmetic operations.
  {
    Strong x = (-Strong(1) + Strong(10) - Strong(1)) & (Strong(0xf) | Strong(0));
    EXPECT_EQ(x, Strong(8));
  }

  // Multiple in-place arithmetic operators.
  {
    Strong x = Strong(1);
    Strong y = Strong(1);
    Strong z = Strong(1);
    x += (y += (z += Strong(1)));
    EXPECT_EQ(z, Strong(2));
    EXPECT_EQ(y, Strong(3));
    EXPECT_EQ(x, Strong(4));
  }

  // Chained multiply / divide operators.
  {
    Strong a = Strong(1);

    EXPECT_EQ(a * 3 * 2 / 3 / Strong(2), 1);
  }

  // Multiple assignment operators.
  {
    Strong x = Strong(1);
    Strong y = Strong(2);
    Strong z = Strong(3);
    x = y = z;
    EXPECT_EQ(x, Strong(3));
    EXPECT_EQ(y, Strong(3));
  }

  // Multiple increment operators.
  {
    Strong a = Strong(1);
    Strong b = Strong(2);
    Strong c = (a++) + (++b);
    EXPECT_EQ(a, Strong(2));
    EXPECT_EQ(b, Strong(3));
    EXPECT_EQ(c, Strong(4));
  }
}

}  // anonymous namespace
