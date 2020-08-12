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

template <typename T>
void RoundUpPowerOfTwo() {
  constexpr T kWidth = std::numeric_limits<T>::digits;
  constexpr T kOne = static_cast<T>(1);
  EXPECT_EQ(kOne, fbl::roundup_pow2(static_cast<T>(0)));
  EXPECT_EQ(kOne, fbl::roundup_pow2(static_cast<T>(1)));
  EXPECT_EQ(kOne << (kWidth - 1), fbl::roundup_pow2(kOne << (kWidth - 1)));
  for (size_t i = 2; i < kWidth - 2; i++) {
    T val = (kOne << i);
    EXPECT_EQ(val, fbl::roundup_pow2(val));
    EXPECT_EQ(val, fbl::roundup_pow2(static_cast<T>(val - 1)));
    EXPECT_EQ(val << kOne, fbl::roundup_pow2(static_cast<T>(val + 1)));
  }

#ifdef __Fuchsia__
  ASSERT_DEATH([] { fbl::roundup_pow2(1 + (kOne << (kWidth - 1))); });
  ASSERT_DEATH([] { fbl::roundup_pow2(std::numeric_limits<T>::max()); });
#endif
}

template <typename T>
void IsPowerOfTwo() {
  T val = 0;
  EXPECT_FALSE(fbl::is_pow2<T>(val));
  EXPECT_FALSE(fbl::is_pow2<T>(static_cast<T>(val - 1)));

  for (val = 1u; val; val = static_cast<T>(val << 1u)) {
    EXPECT_TRUE(fbl::is_pow2<T>(val));
    EXPECT_FALSE(fbl::is_pow2<T>(static_cast<T>(val - 5u)));
    EXPECT_FALSE(fbl::is_pow2<T>(static_cast<T>(val + 5u)));
  }
}
// TODO(fxbug.dev/38140) : Get rid of this macro when there is a better way to expand templated
// tests.
#define IS_POW2_TEST(_type) \
  TEST(AlgorithmTest, IsPow2_##_type) { ASSERT_NO_FAILURES(IsPowerOfTwo<_type>()); }

#define ROUNDUP_POW2_TEST(_type) \
  TEST(AlgorithmTest, RoundUpPow2_##_type) { ASSERT_NO_FAILURES(RoundUpPowerOfTwo<_type>()); }

#if TEST_WILL_NOT_COMPILE || 0
ROUNDUP_POW2_TEST(uint8_t)
#endif
#if TEST_WILL_NOT_COMPILE || 0
ROUNDUP_POW2_TEST(int32_t)
#endif

IS_POW2_TEST(uint8_t)
IS_POW2_TEST(uint16_t)
IS_POW2_TEST(uint32_t)
IS_POW2_TEST(uint64_t)
IS_POW2_TEST(size_t)
ROUNDUP_POW2_TEST(uint32_t)
ROUNDUP_POW2_TEST(uint64_t)
#undef IS_POW2_TEST
#undef ROUNDUP_POW2_TEST

}  // namespace
