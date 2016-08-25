// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
#include <new.h>
#include <stdio.h>
#include <unittest.h>
#include <utils/unique_ptr.h>

static int destroy_count = 0;

struct CountingDeleter {
    void operator()(int* p)
    {
        destroy_count++;
        delete p;
    }
};

using CountingPtr    = utils::unique_ptr<int,   CountingDeleter>;
using CountingArrPtr = utils::unique_ptr<int[], CountingDeleter>;

static bool uptr_test_scoped_destruction(void* context) {
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

static bool uptr_test_move(void* context) {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;
    // Construct and move into another unique_ptr.
    {
        CountingPtr ptr(new (&ac) int);
        EXPECT_TRUE(ac.check(), "");

        CountingPtr ptr2 = utils::move(ptr);
        EXPECT_EQ(ptr.get(), nullptr, "expected ptr to be null");
    }

    EXPECT_EQ(1, destroy_count, "");

    END_TEST;
}

static bool uptr_test_null_scoped_destruction(void* context) {
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

static bool uptr_test_diff_scope_swap(void* context) {
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

static bool uptr_test_bool_op(void* context) {
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

static bool uptr_test_comparison(void* context) {
    BEGIN_TEST;

    AllocChecker ac;
    // Test comparison operators.
    utils::unique_ptr<int> null_unique;
    utils::unique_ptr<int> lesser_unique(new (&ac) int(1));
    EXPECT_TRUE(ac.check(), "");

    utils::unique_ptr<int> greater_unique(new (&ac) int(2));
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

static bool uptr_test_array_scoped_destruction(void* context) {
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

static bool uptr_test_array_move(void* context) {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;
    // Construct and move into another unique_ptr.
    {
        CountingArrPtr ptr(new (&ac) int[1]);
        EXPECT_TRUE(ac.check(), "");

        CountingArrPtr ptr2 = utils::move(ptr);
        EXPECT_EQ(ptr.get(), nullptr, "expected ptr to be null");
    }
    EXPECT_EQ(1, destroy_count, "");

    END_TEST;
}

static bool uptr_test_array_null_scoped_destruction(void* context) {
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

static bool uptr_test_array_diff_scope_swap(void* context) {
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

static bool uptr_test_array_bool_op(void* context) {
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

static bool uptr_test_array_comparison(void* context) {
    BEGIN_TEST;

    AllocChecker ac;

    utils::unique_ptr<int[]> null_unique;
    utils::unique_ptr<int[]> lesser_unique(new (&ac) int[1]);
    EXPECT_TRUE(ac.check(), "");
    utils::unique_ptr<int[]> greater_unique(new (&ac) int[2]);
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

UNITTEST_START_TESTCASE(unique_ptr)
UNITTEST("Scoped Destruction",               uptr_test_scoped_destruction)
UNITTEST("Move",                             uptr_test_move)
UNITTEST("nullptr Scoped Destruction",       uptr_test_null_scoped_destruction)
UNITTEST("Different Scope Swapping",         uptr_test_diff_scope_swap)
UNITTEST("operator bool",                    uptr_test_bool_op)
UNITTEST("comparison operators",             uptr_test_comparison)
UNITTEST("Array Scoped Destruction",         uptr_test_array_scoped_destruction)
UNITTEST("Array Move",                       uptr_test_array_move)
UNITTEST("Array nullptr Scoped Destruction", uptr_test_array_null_scoped_destruction)
UNITTEST("Array Different Scope Swapping",   uptr_test_array_diff_scope_swap)
UNITTEST("Array operator bool",              uptr_test_array_bool_op)
UNITTEST("Array comparison operators",       uptr_test_array_comparison)
UNITTEST_END_TESTCASE(unique_ptr, "uptr", "Tests of the utils::unique_ptr<T> class",
                      NULL, NULL);
