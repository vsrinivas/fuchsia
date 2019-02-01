/*
 * Copyright 2018 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

extern "C" {
#include "bitarr.h"
}

#include "gtest/gtest.h"

TEST(BitArr, FindFirstBit_SubWord) {
    BITARR(arr, 5);
    memset(arr, 0, sizeof(arr));

    EXPECT_EQ(5u, find_first_bit(arr, 5));

    BITARR_SET(arr, 3);
    EXPECT_EQ(3u, find_first_bit(arr, 5));

    BITARR_SET(arr, 0);
    EXPECT_EQ(0u, find_first_bit(arr, 5));
}

TEST(BitArr, FindFirstBit_ExactlyOneWord) {
    BITARR(arr, BITARR_TYPE_NUM_BITS);
    memset(arr, 0, sizeof(arr));

    EXPECT_EQ(BITARR_TYPE_NUM_BITS, find_first_bit(arr, BITARR_TYPE_NUM_BITS));

    BITARR_SET(arr, BITARR_TYPE_NUM_BITS - 1);
    EXPECT_EQ(BITARR_TYPE_NUM_BITS - 1, find_first_bit(arr, BITARR_TYPE_NUM_BITS));

    BITARR_SET(arr, 3);
    EXPECT_EQ(3u, find_first_bit(arr, BITARR_TYPE_NUM_BITS));

    BITARR_SET(arr, 0);
    EXPECT_EQ(0u, find_first_bit(arr, BITARR_TYPE_NUM_BITS));
}

TEST(BitArr, FindFirstBit_TwoWordsAndChange) {
    constexpr size_t n = 2 * BITARR_TYPE_NUM_BITS + 5;
    BITARR(arr, n);
    memset(arr, 0, sizeof(arr));

    EXPECT_EQ(n, find_first_bit(arr, n));

    BITARR_SET(arr, n - 1);
    EXPECT_EQ(n - 1, find_first_bit(arr, n));

    BITARR_SET(arr, 0);
    EXPECT_EQ(0u, find_first_bit(arr, BITARR_TYPE_NUM_BITS));
}

TEST(BitArr, FindNextBit_SubWord) {
    BITARR(arr, 5);
    memset(arr, 0, sizeof(arr));

    for (size_t offset = 0; offset < 6; ++offset) {
        EXPECT_EQ(5u, find_next_bit(arr, 5, offset));
    }

    BITARR_SET(arr, 2);
    EXPECT_EQ(2u, find_next_bit(arr, 5, 0));
    EXPECT_EQ(2u, find_next_bit(arr, 5, 1));
    EXPECT_EQ(2u, find_next_bit(arr, 5, 2));
    EXPECT_EQ(5u, find_next_bit(arr, 5, 3));
    EXPECT_EQ(5u, find_next_bit(arr, 5, 4));
    EXPECT_EQ(5u, find_next_bit(arr, 5, 10));

    BITARR_SET(arr, 0);
    EXPECT_EQ(0u, find_next_bit(arr, 5, 0));
    EXPECT_EQ(2u, find_next_bit(arr, 5, 1));
    EXPECT_EQ(2u, find_next_bit(arr, 5, 2));
    EXPECT_EQ(5u, find_next_bit(arr, 5, 3));
    EXPECT_EQ(5u, find_next_bit(arr, 5, 4));
    EXPECT_EQ(5u, find_next_bit(arr, 5, 10));
}

TEST(BitArr, FindNextBit_ExactlyTwoWords) {
    constexpr size_t n = 2 * BITARR_TYPE_NUM_BITS;
    BITARR(arr, n);
    memset(arr, 0, sizeof(arr));

    for (size_t offset = 0; offset <= n; ++offset) {
        EXPECT_EQ(n, find_next_bit(arr, n, offset));
    }

    BITARR_SET(arr, n - 5);
    EXPECT_EQ(n - 5, find_next_bit(arr, n, 0));
    EXPECT_EQ(n - 5, find_next_bit(arr, n, BITARR_TYPE_NUM_BITS));
    EXPECT_EQ(n - 5, find_next_bit(arr, n, n - 6));
    EXPECT_EQ(n - 5, find_next_bit(arr, n, n - 5));
    EXPECT_EQ(n, find_next_bit(arr, n, n - 4));

    BITARR_SET(arr, 3);
    EXPECT_EQ(3u, find_next_bit(arr, n, 0));
    EXPECT_EQ(3u, find_next_bit(arr, n, 3));
    EXPECT_EQ(n - 5, find_next_bit(arr, n, 4));
    EXPECT_EQ(n - 5, find_next_bit(arr, n, n - 6));
    EXPECT_EQ(n - 5, find_next_bit(arr, n, n - 5));
    EXPECT_EQ(n, find_next_bit(arr, n, n - 4));
}

TEST(BitArr, FindNextBit_TwoWordsAndChange) {
    constexpr size_t n = 2 * BITARR_TYPE_NUM_BITS + 7;
    BITARR(arr, n);
    memset(arr, 0, sizeof(arr));

    for (size_t offset = 0; offset <= n; ++offset) {
        EXPECT_EQ(n, find_next_bit(arr, n, offset));
    }

    BITARR_SET(arr, n - 5);
    EXPECT_EQ(n - 5, find_next_bit(arr, n, 0));
    EXPECT_EQ(n - 5, find_next_bit(arr, n, BITARR_TYPE_NUM_BITS));
    EXPECT_EQ(n - 5, find_next_bit(arr, n, n - 6));
    EXPECT_EQ(n - 5, find_next_bit(arr, n, n - 5));
    EXPECT_EQ(n, find_next_bit(arr, n, n - 4));

    BITARR_SET(arr, BITARR_TYPE_NUM_BITS);
    EXPECT_EQ(BITARR_TYPE_NUM_BITS, find_next_bit(arr, n, 0));
    EXPECT_EQ(BITARR_TYPE_NUM_BITS, find_next_bit(arr, n, BITARR_TYPE_NUM_BITS));
    EXPECT_EQ(n - 5, find_next_bit(arr, n, BITARR_TYPE_NUM_BITS + 1));
    EXPECT_EQ(n - 5, find_next_bit(arr, n, n - 5));
    EXPECT_EQ(n, find_next_bit(arr, n, n - 4));
}

TEST(BitArr, BitarrEmpty) {
    constexpr size_t n = BITARR_TYPE_NUM_BITS + 5;
    BITARR(arr, n);
    memset(arr, 0, sizeof(arr));

    EXPECT_EQ(true, bitarr_empty(arr, n));
    BITARR_SET(arr, n - 2);
    EXPECT_EQ(false, bitarr_empty(arr, n));
}

TEST(BitArr, ForEachSetBit) {
    constexpr size_t n = 2 * BITARR_TYPE_NUM_BITS + 7;
    BITARR(arr, n);
    memset(arr, 0, sizeof(arr));

    BITARR_SET(arr, 0);
    BITARR_SET(arr, 4);
    BITARR_SET(arr, n - 2);

    std::vector<size_t> result;
    size_t bit;
    for_each_set_bit(bit, arr, n) {
        result.push_back(bit);
    }

    ASSERT_EQ(3u, result.size());
    EXPECT_EQ(0u, result[0]);
    EXPECT_EQ(4u, result[1]);
    EXPECT_EQ(n - 2, result[2]);
}

TEST(BitArr, ForEachSetBit_Empty) {
    constexpr size_t n = 2 * BITARR_TYPE_NUM_BITS + 7;
    BITARR(arr, n);
    memset(arr, 0, sizeof(arr));

    std::vector<size_t> result;
    size_t bit;
    for_each_set_bit(bit, arr, n) {
        result.push_back(bit);
    }

    ASSERT_TRUE(result.empty());
}
