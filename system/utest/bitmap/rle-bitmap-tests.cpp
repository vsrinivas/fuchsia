// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitmap/rle-bitmap.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <unittest/unittest.h>

namespace bitmap {
namespace tests {

typedef bool(VerifyCallback)(size_t index, size_t bitoff, size_t bitlen);

static bool VerifyCounts(const RleBitmap& bitmap, size_t rng_expected, size_t bit_expected,
                         VerifyCallback cb) {
    BEGIN_HELPER;
    size_t rng_count = 0;
    size_t bit_count = 0;
    for (auto& range : bitmap) {
        EXPECT_TRUE(cb(rng_count, range.bitoff, range.bitlen));
        rng_count++;
        bit_count += range.bitlen;
    }

    EXPECT_EQ(rng_count, rng_expected, "unexpected range count");
    EXPECT_EQ(rng_count, bitmap.num_ranges(), "unexpected range count");
    EXPECT_EQ(bit_count, bit_expected, "unexpected bit count");
    EXPECT_EQ(bit_count, bitmap.num_bits(), "unexpected bit count");
    END_HELPER;
}
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

    ASSERT_EQ(bitmap.Set(2, 3), ZX_OK, "set bit");
    EXPECT_TRUE(bitmap.Get(2, 3), "get bit after setting");
    EXPECT_EQ(bitmap.num_bits(), 1U, "unexpected bit count");

    auto cb = [](size_t index, size_t bitoff, size_t bitlen) -> bool {
        BEGIN_HELPER;
        EXPECT_EQ(bitoff, 2U, "bitoff");
        EXPECT_EQ(bitlen, 1U, "bitlen");
        END_HELPER;
    };
    EXPECT_TRUE(VerifyCounts(bitmap, 1U, 1U, cb));

    ASSERT_EQ(bitmap.Clear(2, 3), ZX_OK, "clear bit");
    EXPECT_FALSE(bitmap.Get(2, 3), "get bit after clearing");
    EXPECT_TRUE(VerifyCounts(bitmap, 0U, 0U, cb));

    END_TEST;
}

static bool SetTwice(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    ASSERT_EQ(bitmap.SetOne(2), ZX_OK, "set bit");
    EXPECT_TRUE(bitmap.GetOne(2), "get bit after setting");

    EXPECT_EQ(bitmap.num_bits(), 1);

    ASSERT_EQ(bitmap.SetOne(2), ZX_OK, "set bit again");
    EXPECT_TRUE(bitmap.GetOne(2), "get bit after setting again");
    EXPECT_EQ(bitmap.num_bits(), 1);

    auto cb = [](size_t index, size_t bitoff, size_t bitlen) -> bool {
        BEGIN_HELPER;
        EXPECT_EQ(bitoff, 2U, "bitoff");
        EXPECT_EQ(bitlen, 1U, "bitlen");
        END_HELPER;
    };
    EXPECT_TRUE(VerifyCounts(bitmap, 1U, 1U, cb));

    END_TEST;
}

static bool ClearTwice(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    ASSERT_EQ(bitmap.SetOne(2), ZX_OK, "set bit");
    EXPECT_EQ(bitmap.num_bits(), 1U, "unexpected bit count");

    ASSERT_EQ(bitmap.ClearOne(2), ZX_OK, "clear bit");
    EXPECT_FALSE(bitmap.GetOne(2), "get bit after clearing");
    EXPECT_EQ(bitmap.num_bits(), 0U, "unexpected bit count");

    ASSERT_EQ(bitmap.ClearOne(2), ZX_OK, "clear bit again");
    EXPECT_FALSE(bitmap.GetOne(2), "get bit after clearing again");
    EXPECT_EQ(bitmap.num_bits(), 0U, "unexpected bit count");

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

    ASSERT_EQ(bitmap.SetOne(2), ZX_OK, "set bit");
    EXPECT_TRUE(bitmap.Get(2, 3, &first_unset), "get bit after setting");
    EXPECT_EQ(first_unset, 3U, "check returned arg");

    first_unset = 0;
    EXPECT_FALSE(bitmap.Get(2, 4, &first_unset), "get larger range after setting");
    EXPECT_EQ(first_unset, 3U, "check returned arg");

    ASSERT_EQ(bitmap.Set(3, 4), ZX_OK, "set another bit");
    EXPECT_FALSE(bitmap.Get(2, 5, &first_unset), "get larger range after setting another");
    EXPECT_EQ(first_unset, 4U, "check returned arg");

    auto cb = [](size_t index, size_t bitoff, size_t bitlen) -> bool {
        BEGIN_HELPER;
        EXPECT_EQ(bitoff, 2U, "bitoff");
        EXPECT_EQ(bitlen, 2U, "bitlen");
        END_HELPER;
    };
    EXPECT_TRUE(VerifyCounts(bitmap, 1U, 2U, cb));
    END_TEST;
}

static bool SetRange(void) {
    BEGIN_TEST;

    RleBitmap bitmap;
    ASSERT_EQ(bitmap.Set(2, 100), ZX_OK, "set range");
    EXPECT_EQ(bitmap.num_bits(), 98U, "unexpected bit count");

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

    ASSERT_EQ(bitmap.Set(2, 100), ZX_OK, "set range");

    bitmap.ClearAll();

    for (__UNUSED auto& range : bitmap) {
        EXPECT_FALSE(true, "iterating on empty set");
    }

    ASSERT_EQ(bitmap.Set(2, 100), ZX_OK, "set range");

    for (auto& range : bitmap) {
        EXPECT_EQ(range.bitoff, 2U, "bitoff");
        EXPECT_EQ(range.bitlen, 100U - 2U, "bitlen");
    }

    auto cb = [](size_t index, size_t bitoff, size_t bitlen) -> bool {
        BEGIN_HELPER;
        EXPECT_EQ(bitoff, 2U, "bitoff");
        EXPECT_EQ(bitlen, 100U - 2U, "bitlen");
        END_HELPER;
    };

    EXPECT_TRUE(VerifyCounts(bitmap, 1U, 100U - 2U, cb));
    END_TEST;
}

static bool ClearSubrange(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    ASSERT_EQ(bitmap.Set(2, 100), ZX_OK, "set range");
    EXPECT_EQ(bitmap.num_bits(), 98U, "unexpected bit count");
    ASSERT_EQ(bitmap.Clear(50, 80), ZX_OK, "clear range");
    EXPECT_EQ(bitmap.num_bits(), 68U, "unexpected bit count");

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

    auto cb = [](size_t index, size_t bitoff, size_t bitlen) -> bool {
        BEGIN_HELPER;
        if (index == 0) {
            EXPECT_EQ(bitoff, 2U, "bitoff");
            EXPECT_EQ(bitlen, 50U - 2U, "bitlen");
        } else {
            EXPECT_EQ(bitoff, 80U, "bitoff");
            EXPECT_EQ(bitlen, 100U - 80U, "bitlen");
        }
        END_HELPER;
    };

    EXPECT_TRUE(VerifyCounts(bitmap, 2U, 68U, cb));
    END_TEST;
}

static bool MergeRanges(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    constexpr size_t kMaxVal = 100;

    for (size_t i = 0; i < kMaxVal; i += 2) {
        ASSERT_EQ(bitmap.SetOne(i), ZX_OK, "setting even bits");
    }

    auto cb = [](size_t index, size_t bitoff, size_t bitlen) -> bool {
        BEGIN_HELPER;
        EXPECT_EQ(bitoff, 2 * index, "bitoff");
        EXPECT_EQ(bitlen, 1U, "bitlen");
        END_HELPER;
    };

    EXPECT_TRUE(VerifyCounts(bitmap, kMaxVal / 2, kMaxVal / 2, cb));

    for (size_t i = 1; i < kMaxVal; i += 4) {
        ASSERT_EQ(bitmap.SetOne(i), ZX_OK, "setting congruent 1 mod 4 bits");
    }

    auto cb2 = [](size_t index, size_t bitoff, size_t bitlen) -> bool {
        BEGIN_HELPER;
        EXPECT_EQ(bitoff, 4 * index, "bitoff");
        EXPECT_EQ(bitlen, 3U, "bitlen");
        END_HELPER;
    };

    EXPECT_TRUE(VerifyCounts(bitmap, kMaxVal / 4, 3 * kMaxVal / 4, cb2));
    END_TEST;
}

static bool SplitRanges(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    constexpr size_t kMaxVal = 100;
    ASSERT_EQ(bitmap.Set(0, kMaxVal), ZX_OK, "setting all bits");

    for (size_t i = 1; i < kMaxVal; i += 4) {
        ASSERT_EQ(bitmap.ClearOne(i), ZX_OK, "clearing congruent 1 mod 4 bits");
    }

    auto cb = [](size_t index, size_t bitoff, size_t bitlen) -> bool {
        BEGIN_HELPER;
        if (index == 0) {
            EXPECT_EQ(bitoff, 0U, "bitoff");
            EXPECT_EQ(bitlen, 1U, "bitlen");
        } else {
            size_t offset = 4 * index - 2;
            size_t len = fbl::min(size_t(3), kMaxVal - offset);
            EXPECT_EQ(bitoff, offset, "bitoff");
            EXPECT_EQ(bitlen, len, "bitlen");
        }
        END_HELPER;
    };

    EXPECT_TRUE(VerifyCounts(bitmap, kMaxVal / 4 + 1, 3 * kMaxVal / 4, cb));

    for (size_t i = 0; i < kMaxVal; i += 2) {
        ASSERT_EQ(bitmap.ClearOne(i), ZX_OK, "clearing even bits");
    }

    auto cb2 = [](size_t index, size_t bitoff, size_t bitlen) -> bool {
        BEGIN_HELPER;
        EXPECT_EQ(bitoff, 4 * index + 3, "bitoff");
        EXPECT_EQ(bitlen, 1U, "bitlen");
        END_HELPER;
    };

    EXPECT_TRUE(VerifyCounts(bitmap, kMaxVal / 4, kMaxVal / 4, cb2));
    END_TEST;
}

static bool BoundaryArguments(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    EXPECT_EQ(bitmap.Set(0, 0), ZX_OK, "range contains no bits");
    EXPECT_EQ(bitmap.Set(5, 4), ZX_ERR_INVALID_ARGS, "max is less than off");
    EXPECT_EQ(bitmap.Set(5, 5), ZX_OK, "range contains no bits");

    EXPECT_EQ(bitmap.Clear(0, 0), ZX_OK, "range contains no bits");
    EXPECT_EQ(bitmap.Clear(5, 4), ZX_ERR_INVALID_ARGS, "max is less than off");
    EXPECT_EQ(bitmap.Clear(5, 5), ZX_OK, "range contains no bits");

    EXPECT_TRUE(bitmap.Get(0, 0), "range contains no bits, so all are true");
    EXPECT_TRUE(bitmap.Get(5, 4), "range contains no bits, so all are true");
    EXPECT_TRUE(bitmap.Get(5, 5), "range contains no bits, so all are true");

    END_TEST;
}

static bool NoAlloc(void) {
    BEGIN_TEST;

    RleBitmap bitmap;

    EXPECT_EQ(bitmap.SetNoAlloc(0, 65536, nullptr), ZX_ERR_INVALID_ARGS, "set bits with nullptr freelist");
    EXPECT_EQ(bitmap.ClearNoAlloc(0, 65536, nullptr), ZX_ERR_INVALID_ARGS, "clear bits with nullptr freelist");

    RleBitmap::FreeList free_list;
    EXPECT_EQ(bitmap.SetNoAlloc(0, 65536, &free_list), ZX_ERR_NO_MEMORY, "set bits with empty freelist");

    fbl::AllocChecker ac;
    free_list.push_back(fbl::unique_ptr<RleBitmapElement>(new (&ac) RleBitmapElement()));
    ASSERT_TRUE(ac.check(), "alloc check");
    EXPECT_EQ(bitmap.SetNoAlloc(0, 65536, &free_list), ZX_OK, "set bits");
    EXPECT_TRUE(bitmap.Get(0, 65536), "get bit after setting");
    EXPECT_EQ(free_list.size_slow(), 0U, "free list empty after alloc");

    EXPECT_EQ(bitmap.ClearNoAlloc(1, 65535, &free_list), ZX_ERR_NO_MEMORY, "clear bits with empty freelist and alloc needed");

    free_list.push_back(fbl::unique_ptr<RleBitmapElement>(new (&ac) RleBitmapElement()));
    ASSERT_TRUE(ac.check(), "alloc check");
    EXPECT_EQ(bitmap.ClearNoAlloc(1, 65535, &free_list), ZX_OK, "clear bits");
    size_t first_unset = 0;
    EXPECT_FALSE(bitmap.Get(0, 65536, &first_unset), "get bit after clearing");
    EXPECT_EQ(first_unset, 1U, "check first_unset");
    EXPECT_EQ(free_list.size_slow(), 0U, "free list empty after alloc");

    free_list.push_back(fbl::unique_ptr<RleBitmapElement>(new (&ac) RleBitmapElement()));
    ASSERT_TRUE(ac.check(), "alloc check");
    EXPECT_EQ(bitmap.SetNoAlloc(1, 65535, &free_list), ZX_OK, "add range back in");
    EXPECT_EQ(free_list.size_slow(), 2U, "free list has two entries after starting with one and merging two existing ranges");

    EXPECT_EQ(bitmap.ClearNoAlloc(0, 65536, &free_list), ZX_OK, "remove everything we allocated");
    EXPECT_EQ(free_list.size_slow(), 3U, "free list has as many entries as we allocated");

    END_TEST;
}

static bool SetOutOfOrder(void) {
    BEGIN_TEST;
    RleBitmap bitmap;
    EXPECT_EQ(bitmap.Set(0x64, 0x65), ZX_OK, "setting later");
    EXPECT_EQ(bitmap.Set(0x60, 0x61), ZX_OK, "setting earlier");
    EXPECT_EQ(bitmap.num_ranges(), 2U, "unexpected range count");
    EXPECT_EQ(bitmap.num_bits(), 2U, "unexpected bit count");
    EXPECT_TRUE(bitmap.Get(0x64, 0x65), "getting first set");
    EXPECT_TRUE(bitmap.Get(0x60, 0x61), "getting second set");
    END_TEST;
}

static bool VerifyRange(const RleBitmap& bitmap, size_t bitoff, size_t bitmax, size_t min_val,
                        size_t max_val) {
    BEGIN_HELPER;
    size_t out;
    EXPECT_TRUE(bitmap.Get(bitoff, bitmax));
    EXPECT_EQ(bitmap.Find(false, min_val, max_val, bitoff - min_val, &out), ZX_OK);
    EXPECT_EQ(out, min_val);
    EXPECT_EQ(bitmap.Find(false, min_val, max_val, max_val - bitmax, &out), ZX_OK);
    EXPECT_EQ(out, bitmax);
    EXPECT_EQ(bitmap.num_bits(), bitmax - bitoff);
    END_HELPER;
}

static bool VerifyCleared(const RleBitmap& bitmap, size_t min_val, size_t max_val) {
    BEGIN_HELPER;
    size_t out;
    EXPECT_EQ(bitmap.Find(false, min_val, max_val, max_val - min_val, &out), ZX_OK);
    EXPECT_EQ(out, min_val);
    EXPECT_EQ(bitmap.num_bits(), 0);
    END_HELPER;
}

static bool CheckOverlap(size_t bitoff1, size_t bitmax1, size_t bitoff2, size_t bitmax2,
                        size_t min_val, size_t max_val) {
    BEGIN_HELPER;
    EXPECT_GE(bitoff1, min_val);
    EXPECT_GE(bitoff2, min_val);
    EXPECT_LE(bitmax1, max_val);
    EXPECT_LE(bitmax2, max_val);

    RleBitmap bitmap;
    size_t min_off = fbl::min(bitoff1, bitoff2);
    size_t max_max = fbl::max(bitmax1, bitmax2);
    EXPECT_EQ(bitmap.Set(bitoff1, bitmax1), ZX_OK);
    EXPECT_EQ(bitmap.Set(bitoff2, bitmax2), ZX_OK);
    EXPECT_TRUE(VerifyRange(bitmap, min_off, max_max, min_val, max_val));
    EXPECT_EQ(bitmap.Clear(min_off, max_max), ZX_OK);
    EXPECT_TRUE(VerifyCleared(bitmap, min_val, max_val));
    END_HELPER;
}

static bool SetOverlap(void) {
    BEGIN_TEST;
    EXPECT_TRUE(CheckOverlap(5, 6, 4, 5, 0, 100));
    EXPECT_TRUE(CheckOverlap(3, 5, 1, 4, 0, 100));
    EXPECT_TRUE(CheckOverlap(1, 6, 3, 5, 0, 100));
    EXPECT_TRUE(CheckOverlap(20, 30, 10, 20, 0, 100));
    EXPECT_TRUE(CheckOverlap(20, 30, 15, 25, 0, 100));
    EXPECT_TRUE(CheckOverlap(10, 20, 15, 20, 0, 100));
    EXPECT_TRUE(CheckOverlap(10, 20, 15, 25, 0, 100));
    EXPECT_TRUE(CheckOverlap(10, 30, 15, 25, 0, 100));
    EXPECT_TRUE(CheckOverlap(15, 25, 10, 30, 0, 100));
    END_TEST;
}

static bool FindRange(void) {
    BEGIN_TEST;

    size_t out;
    RleBitmap bitmap;

    EXPECT_EQ(bitmap.Set(5, 10), ZX_OK, "setting range");
    EXPECT_EQ(bitmap.num_bits(), 5, "unexpected bit count");
    // Find unset run before range
    EXPECT_EQ(bitmap.Find(false, 0, 15, 5, &out), ZX_OK, "finding range");
    EXPECT_EQ(out, 0, "unexpected bitoff");
    // Find unset run after range
    EXPECT_EQ(bitmap.Find(false, 1, 15, 5, &out), ZX_OK, "finding range");
    EXPECT_EQ(out, 10, "unexpected bitoff");
    // Unset range too large
    EXPECT_EQ(bitmap.Find(false, 0, 15, 6, &out), ZX_ERR_NO_RESOURCES, "finding range");
    EXPECT_EQ(out, 15, "unexpected bitoff");
    // Find entire set range
    EXPECT_EQ(bitmap.Find(true, 0, 15, 5, &out), ZX_OK, "finding range");
    EXPECT_EQ(out, 5, "unexpected bitoff");
    // Find set run within range
    EXPECT_EQ(bitmap.Find(true, 6, 15, 3, &out), ZX_OK, "finding range");
    EXPECT_EQ(out, 6, "unexpected bitoff");
    // Set range too large
    EXPECT_EQ(bitmap.Find(true, 0, 15, 6, &out), ZX_ERR_NO_RESOURCES, "finding range");
    EXPECT_EQ(out, 15, "unexpected bitoff");
    // Set range too large
    EXPECT_EQ(bitmap.Find(true, 0, 8, 4, &out), ZX_ERR_NO_RESOURCES, "finding range");
    EXPECT_EQ(out, 8, "unexpected bitoff");

    EXPECT_EQ(bitmap.Set(20, 30), ZX_OK, "setting range");
    EXPECT_EQ(bitmap.num_bits(), 15, "unexpected bit count");
    // Find unset run after both ranges
    EXPECT_EQ(bitmap.Find(false, 0, 50, 11, &out), ZX_OK, "finding range");
    EXPECT_EQ(out, 30, "unexpected bitoff");
    // Unset range too large
    EXPECT_EQ(bitmap.Find(false, 0, 40, 11, &out), ZX_ERR_NO_RESOURCES, "finding range");
    EXPECT_EQ(out, 40, "unexpected bitoff");
    // Find set run in first range
    EXPECT_EQ(bitmap.Find(true, 0, 50, 5, &out), ZX_OK, "finding range");
    EXPECT_EQ(out, 5, "unexpected bitoff");
    // Find set run in second range
    EXPECT_EQ(bitmap.Find(true, 0, 50, 7, &out), ZX_OK, "finding range");
    EXPECT_EQ(out, 20, "unexpected bitoff");
    // Find set run in second range
    EXPECT_EQ(bitmap.Find(true, 7, 50, 5, &out), ZX_OK, "finding range");
    EXPECT_EQ(out, 20, "unexpected bitoff");
    // Set range too large
    EXPECT_EQ(bitmap.Find(true, 0, 50, 11, &out), ZX_ERR_NO_RESOURCES, "finding range");
    EXPECT_EQ(out, 50, "unexpected bitoff");
    // Set range too large
    EXPECT_EQ(bitmap.Find(true, 35, 50, 6, &out), ZX_ERR_NO_RESOURCES, "finding range");
    EXPECT_EQ(out, 50, "unexpected bitoff");
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
RUN_TEST(SetOverlap)
RUN_TEST(FindRange)
END_TEST_CASE(rle_bitmap_tests);

} // namespace tests
} // namespace bitmap
