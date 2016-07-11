// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
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

    // Construct and let a unique_ptr fall out of scope.
    {
        CountingPtr ptr(new int);
    }

    EXPECT_EQ(1, destroy_count, "");
    END_TEST;
}

static bool uptr_test_move(void* context) {
    BEGIN_TEST;
    destroy_count = 0;

    // Construct and move into another unique_ptr.
    {
        CountingPtr ptr(new int);
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
    {
        CountingPtr ptr1(new int(4));
        {
            CountingPtr ptr2(new int(7));
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

    CountingPtr foo(new int);
    EXPECT_TRUE(static_cast<bool>(foo), "");

    foo.reset();
    EXPECT_EQ(1, destroy_count, "");
    EXPECT_FALSE(static_cast<bool>(foo), "");

    END_TEST;
}

static bool uptr_test_comparison(void* context) {
    BEGIN_TEST;

    // Test comparison operators.
    utils::unique_ptr<int> null_unique;
    utils::unique_ptr<int> lesser_unique(new int(1));
    utils::unique_ptr<int> greater_unique(new int(2));

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

    // Construct and let a unique_ptr fall out of scope.
    {
        CountingArrPtr ptr(new int[1]);
    }
    EXPECT_EQ(1, destroy_count, "");

    END_TEST;
}

static bool uptr_test_array_move(void* context) {
    BEGIN_TEST;
    destroy_count = 0;

    // Construct and move into another unique_ptr.
    {
        CountingArrPtr ptr(new int[1]);
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
    {
        CountingArrPtr ptr1(new int[1]);
        ptr1[0] = 4;
        {
            CountingArrPtr ptr2(new int[1]);
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

    CountingArrPtr foo(new int[1]);
    EXPECT_TRUE(static_cast<bool>(foo), "");

    foo.reset();
    EXPECT_EQ(1, destroy_count, "");
    EXPECT_FALSE(static_cast<bool>(foo), "");

    END_TEST;
}

static bool uptr_test_array_comparison(void* context) {
    BEGIN_TEST;

    utils::unique_ptr<int[]> null_unique;
    utils::unique_ptr<int[]> lesser_unique(new int[1]);
    utils::unique_ptr<int[]> greater_unique(new int[2]);

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

STATIC_UNITTEST_START_TESTCASE(unique_ptr)
STATIC_UNITTEST("Scoped Destruction",               uptr_test_scoped_destruction)
STATIC_UNITTEST("Move",                             uptr_test_move)
STATIC_UNITTEST("nullptr Scoped Destruction",       uptr_test_null_scoped_destruction)
STATIC_UNITTEST("Different Scope Swapping",         uptr_test_diff_scope_swap)
STATIC_UNITTEST("operator bool",                    uptr_test_bool_op)
STATIC_UNITTEST("comparison operators",             uptr_test_comparison)
STATIC_UNITTEST("Array Scoped Destruction",         uptr_test_array_scoped_destruction)
STATIC_UNITTEST("Array Move",                       uptr_test_array_move)
STATIC_UNITTEST("Array nullptr Scoped Destruction", uptr_test_array_null_scoped_destruction)
STATIC_UNITTEST("Array Different Scope Swapping",   uptr_test_array_diff_scope_swap)
STATIC_UNITTEST("Array operator bool",              uptr_test_array_bool_op)
STATIC_UNITTEST("Array comparison operators",       uptr_test_array_comparison)
STATIC_UNITTEST_END_TESTCASE(unique_ptr, "uptr", "Tests of the utils::unique_ptr<T> class",
                             NULL, NULL);
