// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitmap/raw-bitmap.h>

#include <magenta/new.h>
#include <mxtl/algorithm.h>
#include <unittest/unittest.h>

namespace bitmap {
namespace tests {

static bool InitializedEmpty(void) {
    BEGIN_TEST;

    RawBitmap bitmap(0);
    EXPECT_TRUE(bitmap.GetOne(0), "get one bit");
    EXPECT_EQ(bitmap.SetOne(0), ERR_INVALID_ARGS, "set one bit");
    EXPECT_EQ(bitmap.ClearOne(0), ERR_INVALID_ARGS, "clear one bit");

    bitmap.Reset(1);
    EXPECT_FALSE(bitmap.GetOne(0), "get one bit");
    EXPECT_EQ(bitmap.SetOne(0), NO_ERROR, "set one bit");
    EXPECT_EQ(bitmap.ClearOne(0), NO_ERROR, "clear one bit");

    END_TEST;
}

static bool SingleBit(void) {
    BEGIN_TEST;

    RawBitmap bitmap(128);
    EXPECT_FALSE(bitmap.GetOne(2), "get bit before setting");

    ASSERT_EQ(bitmap.SetOne(2), NO_ERROR, "set bit");
    EXPECT_TRUE(bitmap.GetOne(2), "get bit after setting");

    ASSERT_EQ(bitmap.ClearOne(2), NO_ERROR, "clear bit");
    EXPECT_FALSE(bitmap.GetOne(2), "get bit after clearing");

    END_TEST;
}

static bool SetTwice(void) {
    BEGIN_TEST;

    RawBitmap bitmap(128);

    ASSERT_EQ(bitmap.SetOne(2), NO_ERROR, "set bit");
    EXPECT_TRUE(bitmap.GetOne(2), "get bit after setting");

    ASSERT_EQ(bitmap.SetOne(2), NO_ERROR, "set bit again");
    EXPECT_TRUE(bitmap.GetOne(2), "get bit after setting again");

    END_TEST;
}

static bool ClearTwice(void) {
    BEGIN_TEST;

    RawBitmap bitmap(128);

    ASSERT_EQ(bitmap.SetOne(2), NO_ERROR, "set bit");

    ASSERT_EQ(bitmap.ClearOne(2), NO_ERROR, "clear bit");
    EXPECT_FALSE(bitmap.GetOne(2), "get bit after clearing");

    ASSERT_EQ(bitmap.ClearOne(2), NO_ERROR, "clear bit again");
    EXPECT_FALSE(bitmap.GetOne(2), "get bit after clearing again");

    END_TEST;
}

static bool GetReturnArg(void) {
    BEGIN_TEST;

    RawBitmap bitmap(128);

    uint64_t first_unset = 0;
    EXPECT_FALSE(bitmap.Get(2, 3, nullptr), "get bit with null");
    EXPECT_FALSE(bitmap.Get(2, 3, &first_unset), "get bit with nonnull");
    EXPECT_EQ(first_unset, 2U, "check returned arg");

    ASSERT_EQ(bitmap.SetOne(2), NO_ERROR, "set bit");
    EXPECT_TRUE(bitmap.Get(2, 3, &first_unset), "get bit after setting");
    EXPECT_EQ(first_unset, 3U, "check returned arg");

    first_unset = 0;
    EXPECT_FALSE(bitmap.Get(2, 4, &first_unset), "get larger range after setting");
    EXPECT_EQ(first_unset, 3U, "check returned arg");

    ASSERT_EQ(bitmap.SetOne(3), NO_ERROR, "set another bit");
    EXPECT_FALSE(bitmap.Get(2, 5, &first_unset), "get larger range after setting another");
    EXPECT_EQ(first_unset, 4U, "check returned arg");

    END_TEST;
}

static bool SetRange(void) {
    BEGIN_TEST;

    RawBitmap bitmap(128);

    ASSERT_EQ(bitmap.Set(2, 100), NO_ERROR, "set range");

    uint64_t first_unset = 0;
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

    RawBitmap bitmap(128);

    ASSERT_EQ(bitmap.Set(0, 100), NO_ERROR, "set range");

    bitmap.ClearAll();

    uint64_t first = 0;
    EXPECT_FALSE(bitmap.Get(2, 100, &first), "get range");
    EXPECT_EQ(first, 2U, "all clear");

    ASSERT_EQ(bitmap.Set(0, 99), NO_ERROR, "set range");
    EXPECT_FALSE(bitmap.Get(0, 100, &first), "get range");
    EXPECT_EQ(first, 99U, "all clear");

    END_TEST;
}

static bool ClearSubrange(void) {
    BEGIN_TEST;

    RawBitmap bitmap(128);

    ASSERT_EQ(bitmap.Set(2, 100), NO_ERROR, "set range");
    ASSERT_EQ(bitmap.Clear(50, 80), NO_ERROR, "clear range");

    uint64_t first_unset = 0;
    EXPECT_FALSE(bitmap.Get(2, 100, &first_unset), "get whole original range");
    EXPECT_EQ(first_unset, 50U, "check returned arg");

    first_unset = 0;
    EXPECT_TRUE(bitmap.Get(2, 50, &first_unset), "get first half range");
    EXPECT_EQ(first_unset, 50U, "check returned arg");

    EXPECT_TRUE(bitmap.Get(80, 100, &first_unset), "get second half range");
    EXPECT_EQ(first_unset, 100U, "check returned arg");

    EXPECT_FALSE(bitmap.Get(50, 80, &first_unset), "get cleared range");
    EXPECT_EQ(first_unset, 50U, "check returned arg");

    END_TEST;
}

static bool BoundaryArguments(void) {
    BEGIN_TEST;

    RawBitmap bitmap(128);

    EXPECT_EQ(bitmap.Set(0, 0), NO_ERROR, "range contains no bits");
    EXPECT_EQ(bitmap.Set(5, 4), ERR_INVALID_ARGS, "max is less than off");
    EXPECT_EQ(bitmap.Set(5, 5), NO_ERROR, "range contains no bits");

    EXPECT_EQ(bitmap.Clear(0, 0), NO_ERROR, "range contains no bits");
    EXPECT_EQ(bitmap.Clear(5, 4), ERR_INVALID_ARGS, "max is less than off");
    EXPECT_EQ(bitmap.Clear(5, 5), NO_ERROR, "range contains no bits");

    EXPECT_TRUE(bitmap.Get(0, 0), "range contains no bits, so all are true");
    EXPECT_TRUE(bitmap.Get(5, 4), "range contains no bits, so all are true");
    EXPECT_TRUE(bitmap.Get(5, 5), "range contains no bits, so all are true");

    END_TEST;
}

static bool SetOutOfOrder(void) {
    BEGIN_TEST;

    RawBitmap bitmap(128);
    EXPECT_EQ(bitmap.SetOne(0x64), NO_ERROR, "setting later");
    EXPECT_EQ(bitmap.SetOne(0x60), NO_ERROR, "setting earlier");

    EXPECT_TRUE(bitmap.GetOne(0x64), "getting first set");
    EXPECT_TRUE(bitmap.GetOne(0x60), "getting second set");
    END_TEST;
}

BEGIN_TEST_CASE(raw_bitmap_tests)
RUN_TEST(InitializedEmpty)
RUN_TEST(SingleBit)
RUN_TEST(SetTwice)
RUN_TEST(ClearTwice)
RUN_TEST(GetReturnArg)
RUN_TEST(SetRange)
RUN_TEST(ClearSubrange)
RUN_TEST(BoundaryArguments)
RUN_TEST(ClearAll)
RUN_TEST(SetOutOfOrder)
END_TEST_CASE(raw_bitmap_tests);

} // namespace tests
} // namespace bitmap
