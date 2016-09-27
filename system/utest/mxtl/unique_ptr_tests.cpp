// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <magenta/cpp.h>
#include <unittest/unittest.h>
#include <mxtl/type_support.h>
#include <mxtl/unique_ptr.h>

static int destroy_count = 0;

struct CountingDeleter {
    void operator()(int* p)
    {
        destroy_count++;
        delete p;
    }
};

using CountingPtr    = mxtl::unique_ptr<int,   CountingDeleter>;
using CountingArrPtr = mxtl::unique_ptr<int[], CountingDeleter>;

static_assert(mxtl::is_standard_layout<int>::value,
              "mxtl::unique_ptr<T>'s should have a standard layout");
static_assert(mxtl::is_standard_layout<CountingPtr>::value,
              "mxtl::unique_ptr<T>'s should have a standard layout");
static_assert(mxtl::is_standard_layout<int[]>::value,
              "mxtl::unique_ptr<T[]>'s should have a standard layout");
static_assert(mxtl::is_standard_layout<CountingArrPtr>::value,
              "mxtl::unique_ptr<T[]>'s should have a standard layout");

static bool uptr_test_scoped_destruction() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;
    // Construct and let a unique_ptr fall out of scope.
    {
        CountingPtr ptr(new (&ac) int);
        EXPECT_TRUE(ac.check(), "");
    }

    EXPECT_EQ(1, destroy_count, "");
    END_TEST;
}

static bool uptr_test_move() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;
    // Construct and move into another unique_ptr.
    {
        CountingPtr ptr(new (&ac) int);
        EXPECT_TRUE(ac.check(), "");

        CountingPtr ptr2 = mxtl::move(ptr);
        EXPECT_EQ(ptr.get(), nullptr, "expected ptr to be null");
    }

    EXPECT_EQ(1, destroy_count, "");

    END_TEST;
}

static bool uptr_test_null_scoped_destruction() {
    BEGIN_TEST;
    destroy_count = 0;

    // Construct a null unique_ptr and let it fall out of scope - should not call
    // deleter.
    {
        CountingPtr ptr(nullptr);
    }

    EXPECT_EQ(0, destroy_count, "");

    END_TEST;
}

static bool uptr_test_diff_scope_swap() {
    BEGIN_TEST;
    destroy_count = 0;

    // Construct a pair of unique_ptrs in different scopes, swap them, and verify
    // that the values change places and that the values are destroyed at the
    // correct times.

    AllocChecker ac;
    {
        CountingPtr ptr1(new (&ac) int(4));
        EXPECT_TRUE(ac.check(), "");
        {
            CountingPtr ptr2(new (&ac) int(7));
            EXPECT_TRUE(ac.check(), "");

            ptr1.swap(ptr2);
            EXPECT_EQ(7, *ptr1, "");
            EXPECT_EQ(4, *ptr2, "");
        }
        EXPECT_EQ(1, destroy_count, "");
    }
    EXPECT_EQ(2, destroy_count, "");

    END_TEST;
}

static bool uptr_test_bool_op() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;

    CountingPtr foo(new (&ac) int);
    EXPECT_TRUE(ac.check(), "");
    EXPECT_TRUE(static_cast<bool>(foo), "");

    foo.reset();
    EXPECT_EQ(1, destroy_count, "");
    EXPECT_FALSE(static_cast<bool>(foo), "");

    END_TEST;
}

static bool uptr_test_comparison() {
    BEGIN_TEST;

    AllocChecker ac;
    // Test comparison operators.
    mxtl::unique_ptr<int> null_unique;
    mxtl::unique_ptr<int> lesser_unique(new (&ac) int(1));
    EXPECT_TRUE(ac.check(), "");

    mxtl::unique_ptr<int> greater_unique(new (&ac) int(2));
    EXPECT_TRUE(ac.check(), "");

    EXPECT_NEQ(lesser_unique.get(), greater_unique.get(), "");
    if (lesser_unique.get() > greater_unique.get())
        lesser_unique.swap(greater_unique);

    // Comparison against nullptr
    EXPECT_TRUE(   null_unique == nullptr, "");
    EXPECT_TRUE( lesser_unique != nullptr, "");
    EXPECT_TRUE(greater_unique != nullptr, "");

    EXPECT_TRUE(nullptr ==    null_unique, "");
    EXPECT_TRUE(nullptr !=  lesser_unique, "");
    EXPECT_TRUE(nullptr != greater_unique, "");

    // Comparison against other unique_ptr<>s
    EXPECT_TRUE( lesser_unique  ==  lesser_unique, "");
    EXPECT_FALSE( lesser_unique == greater_unique, "");
    EXPECT_FALSE(greater_unique ==  lesser_unique, "");
    EXPECT_TRUE(greater_unique  == greater_unique, "");

    EXPECT_FALSE( lesser_unique !=  lesser_unique, "");
    EXPECT_TRUE ( lesser_unique != greater_unique, "");
    EXPECT_TRUE (greater_unique !=  lesser_unique, "");
    EXPECT_FALSE(greater_unique != greater_unique, "");

    EXPECT_FALSE( lesser_unique <   lesser_unique, "");
    EXPECT_TRUE ( lesser_unique <  greater_unique, "");
    EXPECT_FALSE(greater_unique <   lesser_unique, "");
    EXPECT_FALSE(greater_unique <  greater_unique, "");

    EXPECT_FALSE( lesser_unique >   lesser_unique, "");
    EXPECT_FALSE( lesser_unique >  greater_unique, "");
    EXPECT_TRUE (greater_unique >   lesser_unique, "");
    EXPECT_FALSE(greater_unique >  greater_unique, "");

    EXPECT_TRUE ( lesser_unique <=  lesser_unique, "");
    EXPECT_TRUE ( lesser_unique <= greater_unique, "");
    EXPECT_FALSE(greater_unique <=  lesser_unique, "");
    EXPECT_TRUE (greater_unique <= greater_unique, "");

    EXPECT_TRUE ( lesser_unique >=  lesser_unique, "");
    EXPECT_FALSE( lesser_unique >= greater_unique, "");
    EXPECT_TRUE (greater_unique >=  lesser_unique, "");
    EXPECT_TRUE (greater_unique >= greater_unique, "");

    END_TEST;
}

static bool uptr_test_array_scoped_destruction() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;
    // Construct and let a unique_ptr fall out of scope.
    {
        CountingArrPtr ptr(new (&ac) int[1]);
        EXPECT_TRUE(ac.check(), "");
    }
    EXPECT_EQ(1, destroy_count, "");

    END_TEST;
}

static bool uptr_test_array_move() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;
    // Construct and move into another unique_ptr.
    {
        CountingArrPtr ptr(new (&ac) int[1]);
        EXPECT_TRUE(ac.check(), "");

        CountingArrPtr ptr2 = mxtl::move(ptr);
        EXPECT_EQ(ptr.get(), nullptr, "expected ptr to be null");
    }
    EXPECT_EQ(1, destroy_count, "");

    END_TEST;
}

static bool uptr_test_array_null_scoped_destruction() {
    BEGIN_TEST;
    destroy_count = 0;

    // Construct a null unique_ptr and let it fall out of scope - should not call
    // deleter.
    {
        CountingArrPtr ptr(nullptr);
    }
    EXPECT_EQ(0, destroy_count, "");

    END_TEST;
}

static bool uptr_test_array_diff_scope_swap() {
    BEGIN_TEST;
    destroy_count = 0;

    // Construct a pair of unique_ptrs in different scopes, swap them, and verify
    // that the values change places and that the values are destroyed at the
    // correct times.
    AllocChecker ac;

    {
        CountingArrPtr ptr1(new (&ac) int[1]);
        EXPECT_TRUE(ac.check(), "");

        ptr1[0] = 4;
        {
            CountingArrPtr ptr2(new (&ac) int[1]);
            EXPECT_TRUE(ac.check(), "");

            ptr2[0] = 7;
            ptr1.swap(ptr2);
            EXPECT_EQ(7, ptr1[0], "");
            EXPECT_EQ(4, ptr2[0], "");
        }
        EXPECT_EQ(1, destroy_count, "");
    }
    EXPECT_EQ(2, destroy_count, "");

    END_TEST;
}

static bool uptr_test_array_bool_op() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;

    CountingArrPtr foo(new (&ac) int[1]);
    EXPECT_TRUE(ac.check(), "");
    EXPECT_TRUE(static_cast<bool>(foo), "");

    foo.reset();
    EXPECT_EQ(1, destroy_count, "");
    EXPECT_FALSE(static_cast<bool>(foo), "");

    END_TEST;
}

static bool uptr_test_array_comparison() {
    BEGIN_TEST;

    AllocChecker ac;

    mxtl::unique_ptr<int[]> null_unique;
    mxtl::unique_ptr<int[]> lesser_unique(new (&ac) int[1]);
    EXPECT_TRUE(ac.check(), "");
    mxtl::unique_ptr<int[]> greater_unique(new (&ac) int[2]);
    EXPECT_TRUE(ac.check(), "");

    EXPECT_NEQ(lesser_unique.get(), greater_unique.get(), "");
    if (lesser_unique.get() > greater_unique.get())
        lesser_unique.swap(greater_unique);

    // Comparison against nullptr
    EXPECT_TRUE(   null_unique == nullptr, "");
    EXPECT_TRUE( lesser_unique != nullptr, "");
    EXPECT_TRUE(greater_unique != nullptr, "");

    EXPECT_TRUE(nullptr ==    null_unique, "");
    EXPECT_TRUE(nullptr !=  lesser_unique, "");
    EXPECT_TRUE(nullptr != greater_unique, "");

    // Comparison against other unique_ptr<>s
    EXPECT_TRUE( lesser_unique  ==  lesser_unique, "");
    EXPECT_FALSE( lesser_unique == greater_unique, "");
    EXPECT_FALSE(greater_unique ==  lesser_unique, "");
    EXPECT_TRUE(greater_unique  == greater_unique, "");

    EXPECT_FALSE( lesser_unique !=  lesser_unique, "");
    EXPECT_TRUE ( lesser_unique != greater_unique, "");
    EXPECT_TRUE (greater_unique !=  lesser_unique, "");
    EXPECT_FALSE(greater_unique != greater_unique, "");

    EXPECT_FALSE( lesser_unique <   lesser_unique, "");
    EXPECT_TRUE ( lesser_unique <  greater_unique, "");
    EXPECT_FALSE(greater_unique <   lesser_unique, "");
    EXPECT_FALSE(greater_unique <  greater_unique, "");

    EXPECT_FALSE( lesser_unique >   lesser_unique, "");
    EXPECT_FALSE( lesser_unique >  greater_unique, "");
    EXPECT_TRUE (greater_unique >   lesser_unique, "");
    EXPECT_FALSE(greater_unique >  greater_unique, "");

    EXPECT_TRUE ( lesser_unique <=  lesser_unique, "");
    EXPECT_TRUE ( lesser_unique <= greater_unique, "");
    EXPECT_FALSE(greater_unique <=  lesser_unique, "");
    EXPECT_TRUE (greater_unique <= greater_unique, "");

    EXPECT_TRUE ( lesser_unique >=  lesser_unique, "");
    EXPECT_FALSE( lesser_unique >= greater_unique, "");
    EXPECT_TRUE (greater_unique >=  lesser_unique, "");
    EXPECT_TRUE (greater_unique >= greater_unique, "");

    END_TEST;
}

BEGIN_TEST_CASE(unique_ptr)
RUN_NAMED_TEST("Scoped Destruction",               uptr_test_scoped_destruction)
RUN_NAMED_TEST("Move",                             uptr_test_move)
RUN_NAMED_TEST("nullptr Scoped Destruction",       uptr_test_null_scoped_destruction)
RUN_NAMED_TEST("Different Scope Swapping",         uptr_test_diff_scope_swap)
RUN_NAMED_TEST("operator bool",                    uptr_test_bool_op)
RUN_NAMED_TEST("comparison operators",             uptr_test_comparison)
RUN_NAMED_TEST("Array Scoped Destruction",         uptr_test_array_scoped_destruction)
RUN_NAMED_TEST("Array Move",                       uptr_test_array_move)
RUN_NAMED_TEST("Array nullptr Scoped Destruction", uptr_test_array_null_scoped_destruction)
RUN_NAMED_TEST("Array Different Scope Swapping",   uptr_test_array_diff_scope_swap)
RUN_NAMED_TEST("Array operator bool",              uptr_test_array_bool_op)
RUN_NAMED_TEST("Array comparison operators",       uptr_test_array_comparison)
END_TEST_CASE(unique_ptr);
