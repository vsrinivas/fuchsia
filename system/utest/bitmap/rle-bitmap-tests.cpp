// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitmap/rle-bitmap.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <unittest/unittest.h>

namespace bitmap {
namespace tests {

static bool InitializedEmpty(void) {
    BEGIN_TEST;

    RleBitmap bitmap;
    EXPECT_FALSE(bitmap.Get(5, 6), "get one bit");
    for (__UNUSED auto& range : bitmap) {
        EXPECT_FALSE(true, "iterating on empty set");
    }

    END_TEST;
}

static bool SingleBit(void) {
    BEGIN_TEST;

    RleBitmap bitmap;
    EXPECT_FALSE(bitmap.Get(2, 3), "get bit before setting");

    ASSERT_EQ(bitmap.Set(2, 3), MX_OK, "set bit");
    EXPECT_TRUE(bitmap.Get(2, 3), "get bit after setting");

    size_t count = 0;
    for (auto& range : bitmap) {
        EXPECT_EQ(range.bitoff, 2U, "bitoff");
        EXPECT_EQ(range.bitlen, 1U, "bitlen");
        count++;
    }
    EXPECT_EQ(count, 1U, "bitmap has single range");
    EXPECT_EQ(count, bitmap.num_ranges(), "count is number of elements");

    ASSERT_EQ(bitmap.Clear(2, 3), MX_OK, "clear bit");
    EXPECT_FALSE(bitmap.Get(2, 3), "get bit after clearing");

    END_TEST;
}

static bool SetTwice(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    ASSERT_EQ(bitmap.SetOne(2), MX_OK, "set bit");
    EXPECT_TRUE(bitmap.GetOne(2), "get bit after setting");

    ASSERT_EQ(bitmap.SetOne(2), MX_OK, "set bit again");
    EXPECT_TRUE(bitmap.GetOne(2), "get bit after setting again");

    size_t count = 0;
    for (auto& range : bitmap) {
        EXPECT_EQ(range.bitoff, 2U, "bitoff");
        EXPECT_EQ(range.bitlen, 1U, "bitlen");
        count++;
    }
    EXPECT_EQ(count, 1U, "bitmap has single range");
    EXPECT_EQ(count, bitmap.num_ranges(), "count is number of elements");

    END_TEST;
}

static bool ClearTwice(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    ASSERT_EQ(bitmap.SetOne(2), MX_OK, "set bit");

    ASSERT_EQ(bitmap.ClearOne(2), MX_OK, "clear bit");
    EXPECT_FALSE(bitmap.GetOne(2), "get bit after clearing");

    ASSERT_EQ(bitmap.ClearOne(2), MX_OK, "clear bit again");
    EXPECT_FALSE(bitmap.GetOne(2), "get bit after clearing again");

    for (__UNUSED auto& range : bitmap) {
        EXPECT_FALSE(true, "iterating on empty set");
    }

    END_TEST;
}

static bool GetReturnArg(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    size_t first_unset = 0;
    EXPECT_FALSE(bitmap.Get(2, 3, nullptr), "get bit with null");
    EXPECT_FALSE(bitmap.Get(2, 3, &first_unset), "get bit with nonnull");
    EXPECT_EQ(first_unset, 2U, "check returned arg");

    ASSERT_EQ(bitmap.SetOne(2), MX_OK, "set bit");
    EXPECT_TRUE(bitmap.Get(2, 3, &first_unset), "get bit after setting");
    EXPECT_EQ(first_unset, 3U, "check returned arg");

    first_unset = 0;
    EXPECT_FALSE(bitmap.Get(2, 4, &first_unset), "get larger range after setting");
    EXPECT_EQ(first_unset, 3U, "check returned arg");

    ASSERT_EQ(bitmap.Set(3, 4), MX_OK, "set another bit");
    EXPECT_FALSE(bitmap.Get(2, 5, &first_unset), "get larger range after setting another");
    EXPECT_EQ(first_unset, 4U, "check returned arg");

    END_TEST;
}

static bool SetRange(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    ASSERT_EQ(bitmap.Set(2, 100), MX_OK, "set range");

    size_t first_unset = 0;
    EXPECT_TRUE(bitmap.Get(2, 3, &first_unset), "get first bit in range");
    EXPECT_EQ(first_unset, 3U, "check returned arg");

    EXPECT_TRUE(bitmap.Get(99, 100, &first_unset), "get last bit in range");
    EXPECT_EQ(first_unset, 100U, "check returned arg");

    EXPECT_FALSE(bitmap.Get(1, 2, &first_unset), "get bit before first in range");
    EXPECT_EQ(first_unset, 1U, "check returned arg");

    EXPECT_FALSE(bitmap.Get(100, 101, &first_unset), "get bit after last in range");
    EXPECT_EQ(first_unset, 100U, "check returned arg");

    EXPECT_TRUE(bitmap.Get(2, 100, &first_unset), "get entire range");
    EXPECT_EQ(first_unset, 100U, "check returned arg");

    EXPECT_TRUE(bitmap.Get(50, 80, &first_unset), "get part of range");
    EXPECT_EQ(first_unset, 80U, "check returned arg");

    END_TEST;
}

static bool ClearAll(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    ASSERT_EQ(bitmap.Set(2, 100), MX_OK, "set range");

    bitmap.ClearAll();

    for (__UNUSED auto& range : bitmap) {
        EXPECT_FALSE(true, "iterating on empty set");
    }

    ASSERT_EQ(bitmap.Set(2, 100), MX_OK, "set range");
    for (auto& range : bitmap) {
        EXPECT_EQ(range.bitoff, 2U, "bitoff");
        EXPECT_EQ(range.bitlen, 100U - 2U, "bitlen");
    }

    END_TEST;
}

static bool ClearSubrange(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    ASSERT_EQ(bitmap.Set(2, 100), MX_OK, "set range");
    ASSERT_EQ(bitmap.Clear(50, 80), MX_OK, "clear range");

    size_t first_unset = 0;
    EXPECT_FALSE(bitmap.Get(2, 100, &first_unset), "get whole original range");
    EXPECT_EQ(first_unset, 50U, "check returned arg");

    first_unset = 0;
    EXPECT_TRUE(bitmap.Get(2, 50, &first_unset), "get first half range");
    EXPECT_EQ(first_unset, 50U, "check returned arg");

    EXPECT_TRUE(bitmap.Get(80, 100, &first_unset), "get second half range");
    EXPECT_EQ(first_unset, 100U, "check returned arg");

    EXPECT_FALSE(bitmap.Get(50, 80, &first_unset), "get cleared range");
    EXPECT_EQ(first_unset, 50U, "check returned arg");

    size_t count = 0;
    for (auto& range : bitmap) {
        if (count == 0) {
            EXPECT_EQ(range.bitoff, 2U, "bitoff");
            EXPECT_EQ(range.bitlen, 50U - 2U, "bitlen");
        } else {
            EXPECT_EQ(range.bitoff, 80U, "bitoff");
            EXPECT_EQ(range.bitlen, 100U - 80U, "bitlen");
        }
        count++;
    }
    EXPECT_EQ(count, 2U, "check range count");
    EXPECT_EQ(count, bitmap.num_ranges(), "count is number of elements");

    END_TEST;
}

static bool MergeRanges(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    constexpr size_t kMaxVal = 100;

    for (size_t i = 0; i < kMaxVal; i += 2) {
        ASSERT_EQ(bitmap.SetOne(i), MX_OK, "setting even bits");
    }

    size_t count = 0;
    for (auto& range : bitmap) {
        EXPECT_EQ(range.bitoff, 2 * count, "bitoff");
        EXPECT_EQ(range.bitlen, 1U, "bitlen");
        count++;
    }
    EXPECT_EQ(count, kMaxVal / 2, "check range count");

    for (size_t i = 1; i < kMaxVal; i += 4) {
        ASSERT_EQ(bitmap.SetOne(i), MX_OK, "setting congruent 1 mod 4 bits");
    }

    count = 0;
    for (auto& range : bitmap) {
        EXPECT_EQ(range.bitoff, 4 * count, "bitoff");
        EXPECT_EQ(range.bitlen, 3U, "bitlen");
        count++;
    }
    EXPECT_EQ(count, kMaxVal / 4, "check range count");
    EXPECT_EQ(count, bitmap.num_ranges(), "count is number of elements");

    END_TEST;
}

static bool SplitRanges(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    constexpr size_t kMaxVal = 100;
    ASSERT_EQ(bitmap.Set(0, kMaxVal), MX_OK, "setting all bits");

    for (size_t i = 1; i < kMaxVal; i += 4) {
        ASSERT_EQ(bitmap.ClearOne(i), MX_OK, "clearing congruent 1 mod 4 bits");
    }

    size_t count = 0;
    for (auto& range : bitmap) {
        if (count == 0) {
            EXPECT_EQ(range.bitoff, 0U, "bitoff");
            EXPECT_EQ(range.bitlen, 1U, "bitlen");
        } else {
            size_t offset = 4 * count - 2;
            size_t len = fbl::min(size_t(3), kMaxVal - offset);
            EXPECT_EQ(range.bitoff, offset, "bitoff");
            EXPECT_EQ(range.bitlen, len, "bitlen");
        }
        count++;
    }
    EXPECT_EQ(count, kMaxVal / 4 + 1, "check range count");
    EXPECT_EQ(count, bitmap.num_ranges(), "count is number of elements");

    for (size_t i = 0; i < kMaxVal; i += 2) {
        ASSERT_EQ(bitmap.ClearOne(i), MX_OK, "clearing even bits");
    }

    count = 0;
    for (auto& range : bitmap) {
        EXPECT_EQ(range.bitoff, 4 * count + 3, "bitoff");
        EXPECT_EQ(range.bitlen, 1U, "bitlen");
        count++;
    }
    EXPECT_EQ(count, kMaxVal / 4, "check range count");
    EXPECT_EQ(count, bitmap.num_ranges(), "count is number of elements");

    END_TEST;
}

static bool BoundaryArguments(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    EXPECT_EQ(bitmap.Set(0, 0), MX_OK, "range contains no bits");
    EXPECT_EQ(bitmap.Set(5, 4), MX_ERR_INVALID_ARGS, "max is less than off");
    EXPECT_EQ(bitmap.Set(5, 5), MX_OK, "range contains no bits");

    EXPECT_EQ(bitmap.Clear(0, 0), MX_OK, "range contains no bits");
    EXPECT_EQ(bitmap.Clear(5, 4), MX_ERR_INVALID_ARGS, "max is less than off");
    EXPECT_EQ(bitmap.Clear(5, 5), MX_OK, "range contains no bits");

    EXPECT_TRUE(bitmap.Get(0, 0), "range contains no bits, so all are true");
    EXPECT_TRUE(bitmap.Get(5, 4), "range contains no bits, so all are true");
    EXPECT_TRUE(bitmap.Get(5, 5), "range contains no bits, so all are true");

    END_TEST;
}

static bool NoAlloc(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    EXPECT_EQ(bitmap.SetNoAlloc(0, 65536, nullptr), MX_ERR_INVALID_ARGS, "set bits with nullptr freelist");
    EXPECT_EQ(bitmap.ClearNoAlloc(0, 65536, nullptr), MX_ERR_INVALID_ARGS, "clear bits with nullptr freelist");

    RleBitmap::FreeList free_list;
    EXPECT_EQ(bitmap.SetNoAlloc(0, 65536, &free_list), MX_ERR_NO_MEMORY, "set bits with empty freelist");

    fbl::AllocChecker ac;
    free_list.push_back(fbl::unique_ptr<RleBitmapElement>(new (&ac) RleBitmapElement()));
    ASSERT_TRUE(ac.check(), "alloc check");
    EXPECT_EQ(bitmap.SetNoAlloc(0, 65536, &free_list), MX_OK, "set bits");
    EXPECT_TRUE(bitmap.Get(0, 65536), "get bit after setting");
    EXPECT_EQ(free_list.size_slow(), 0U, "free list empty after alloc");

    EXPECT_EQ(bitmap.ClearNoAlloc(1, 65535, &free_list), MX_ERR_NO_MEMORY, "clear bits with empty freelist and alloc needed");

    free_list.push_back(fbl::unique_ptr<RleBitmapElement>(new (&ac) RleBitmapElement()));
    ASSERT_TRUE(ac.check(), "alloc check");
    EXPECT_EQ(bitmap.ClearNoAlloc(1, 65535, &free_list), MX_OK, "clear bits");
    size_t first_unset = 0;
    EXPECT_FALSE(bitmap.Get(0, 65536, &first_unset), "get bit after clearing");
    EXPECT_EQ(first_unset, 1U, "check first_unset");
    EXPECT_EQ(free_list.size_slow(), 0U, "free list empty after alloc");

    free_list.push_back(fbl::unique_ptr<RleBitmapElement>(new (&ac) RleBitmapElement()));
    ASSERT_TRUE(ac.check(), "alloc check");
    EXPECT_EQ(bitmap.SetNoAlloc(1, 65535, &free_list), MX_OK, "add range back in");
    EXPECT_EQ(free_list.size_slow(), 2U, "free list has two entries after starting with one and merging two existing ranges");

    EXPECT_EQ(bitmap.ClearNoAlloc(0, 65536, &free_list), MX_OK, "remove everything we allocated");
    EXPECT_EQ(free_list.size_slow(), 3U, "free list has as many entries as we allocated");

    END_TEST;
}

static bool SetOutOfOrder(void) {
    BEGIN_TEST;

    RleBitmap bitmap;
    EXPECT_EQ(bitmap.Set(0x64, 0x65), MX_OK, "setting later");
    EXPECT_EQ(bitmap.Set(0x60, 0x61), MX_OK, "setting earlier");

    EXPECT_TRUE(bitmap.Get(0x64, 0x65), "getting first set");
    EXPECT_TRUE(bitmap.Get(0x60, 0x61), "getting second set");
    END_TEST;
}

BEGIN_TEST_CASE(rle_bitmap_tests)
RUN_TEST(InitializedEmpty)
RUN_TEST(SingleBit)
RUN_TEST(SetTwice)
RUN_TEST(ClearTwice)
RUN_TEST(GetReturnArg)
RUN_TEST(SetRange)
RUN_TEST(ClearSubrange)
RUN_TEST(MergeRanges)
RUN_TEST(SplitRanges)
RUN_TEST(BoundaryArguments)
RUN_TEST(NoAlloc)
RUN_TEST(ClearAll)
RUN_TEST(SetOutOfOrder)
END_TEST_CASE(rle_bitmap_tests);

} // namespace tests
} // namespace bitmap
