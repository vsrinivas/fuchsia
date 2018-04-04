// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <limits.h>
#include <pow2.h>

static bool pow2_test(void)
{
    const size_t num_uint_bits = sizeof(uint) * CHAR_BIT;
    const size_t num_ulong_bits = sizeof(ulong) * CHAR_BIT;

    BEGIN_TEST;
    EXPECT_EQ(0u, log2_uint_floor(0), "");
    EXPECT_EQ(0u, log2_uint_floor(1), "");
    EXPECT_EQ(1u, log2_uint_floor(2), "");
    EXPECT_EQ(1u, log2_uint_floor(3), "");
    EXPECT_EQ(2u, log2_uint_floor(4), "");
    EXPECT_EQ(num_uint_bits - 2, log2_uint_floor(UINT_MAX >> 1), "");
    EXPECT_EQ(num_uint_bits - 1, log2_uint_floor((UINT_MAX >> 1) + 1), "");
    EXPECT_EQ(num_uint_bits - 1, log2_uint_floor(UINT_MAX), "");

    EXPECT_EQ(0u, log2_uint_ceil(0), "");
    EXPECT_EQ(0u, log2_uint_ceil(1), "");
    EXPECT_EQ(1u, log2_uint_ceil(2), "");
    EXPECT_EQ(2u, log2_uint_ceil(3), "");
    EXPECT_EQ(2u, log2_uint_ceil(4), "");
    EXPECT_EQ(3u, log2_uint_ceil(5), "");
    EXPECT_EQ(num_uint_bits - 1, log2_uint_ceil(UINT_MAX >> 1), "");
    EXPECT_EQ(num_uint_bits - 1, log2_uint_ceil((UINT_MAX >> 1) + 1), "");
    EXPECT_EQ(num_uint_bits, log2_uint_ceil((UINT_MAX >> 1) + 2), "");
    EXPECT_EQ(num_uint_bits, log2_uint_ceil(UINT_MAX), "");

    EXPECT_EQ(0u, log2_ulong_floor(0), "");
    EXPECT_EQ(0u, log2_ulong_floor(1), "");
    EXPECT_EQ(1u, log2_ulong_floor(2), "");
    EXPECT_EQ(1u, log2_ulong_floor(3), "");
    EXPECT_EQ(2u, log2_ulong_floor(4), "");
    EXPECT_EQ(num_ulong_bits - 2, log2_ulong_floor(ULONG_MAX >> 1), "");
    EXPECT_EQ(num_ulong_bits - 1, log2_ulong_floor((ULONG_MAX >> 1) + 1), "");
    EXPECT_EQ(num_ulong_bits - 1, log2_ulong_floor(ULONG_MAX), "");

    EXPECT_EQ(0u, log2_ulong_ceil(0), "");
    EXPECT_EQ(0u, log2_ulong_ceil(1), "");
    EXPECT_EQ(1u, log2_ulong_ceil(2), "");
    EXPECT_EQ(2u, log2_ulong_ceil(3), "");
    EXPECT_EQ(2u, log2_ulong_ceil(4), "");
    EXPECT_EQ(3u, log2_ulong_ceil(5), "");
    EXPECT_EQ(num_ulong_bits - 1, log2_ulong_ceil(ULONG_MAX >> 1), "");
    EXPECT_EQ(num_ulong_bits - 1, log2_ulong_ceil((ULONG_MAX >> 1) + 1), "");
    EXPECT_EQ(num_ulong_bits, log2_ulong_ceil((ULONG_MAX >> 1) + 2), "");
    EXPECT_EQ(num_ulong_bits, log2_ulong_ceil(ULONG_MAX), "");

    EXPECT_TRUE(ispow2(0), "");
    for (size_t i = 0; i < num_uint_bits; ++i) {
        EXPECT_TRUE(ispow2(1u << i), "");
    }
    EXPECT_FALSE(ispow2(3), "");
    EXPECT_FALSE(ispow2(5), "");
    EXPECT_FALSE(ispow2(6), "");
    EXPECT_FALSE(ispow2(7), "");
    EXPECT_FALSE(ispow2(UINT_MAX), "");

    EXPECT_EQ(0u, round_up_pow2_u32(0), "");
    EXPECT_EQ(1u, round_up_pow2_u32(1), "");
    EXPECT_EQ(2u, round_up_pow2_u32(2), "");
    EXPECT_EQ(4u, round_up_pow2_u32(3), "");
    EXPECT_EQ(8u, round_up_pow2_u32(5), "");
    EXPECT_EQ(8u, round_up_pow2_u32(7), "");
    EXPECT_EQ(1u << 31, round_up_pow2_u32(1u << 31), "");
    EXPECT_EQ(0u, round_up_pow2_u32((1u << 31) + 1), "");
    EXPECT_EQ(0u, round_up_pow2_u32(UINT_MAX), "");
    END_TEST;
}

UNITTEST_START_TESTCASE(pow2_tests)
UNITTEST("pow2 lib tests", pow2_test)
UNITTEST_END_TESTCASE(pow2_tests, "pow2", "pow2 lib tests");
