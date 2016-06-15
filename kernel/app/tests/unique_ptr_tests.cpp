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

extern "C" int unique_ptr_tests(int argc, const cmd_args* argv)
{
    BEGIN_TEST;
    // Test for unique_ptr<T, ...> variant
    using CountingPtr = utils::unique_ptr<int, CountingDeleter>;

    // Construct and let a unique_ptr fall out of scope.
    {
        CountingPtr ptr(new int);
    }

    EXPECT_EQ(1, destroy_count, "");

    destroy_count = 0;

    // Construct and move into another unique_ptr.
    {
        CountingPtr ptr(new int);
        CountingPtr ptr2 = utils::move(ptr);
        if (ptr.get() != nullptr) {
          printf("assert failed: expected ptr to be null on line %d\n", __LINE__);
          return 1;
        }
    }

    EXPECT_EQ(1, destroy_count, "");

    destroy_count = 0;

    // Construct a null unique_ptr and let it fall out of scope - should not call
    // deleter.
    {
        CountingPtr ptr(nullptr);
    }

    EXPECT_EQ(0, destroy_count, "");

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

    destroy_count = 0;

    // Test operator bool
    {
        CountingPtr foo(new int);
        EXPECT_TRUE(static_cast<bool>(foo), "");

        foo.reset();
        EXPECT_EQ(1, destroy_count, "");
        EXPECT_FALSE(static_cast<bool>(foo), "");
    }

    destroy_count = 0;

    // Test for unique_ptr<T[], ...> variant
    using CountingArrPtr = utils::unique_ptr<int[], CountingDeleter>;

    // Construct and let a unique_ptr fall out of scope.
    {
        CountingArrPtr ptr(new int[1]);
    }

    EXPECT_EQ(1, destroy_count, "");

    destroy_count = 0;

    // Construct and move into another unique_ptr.
    {
        CountingArrPtr ptr(new int[1]);
        CountingArrPtr ptr2 = utils::move(ptr);
    }

    EXPECT_EQ(1, destroy_count, "");

    destroy_count = 0;

    // Construct a null unique_ptr and let it fall out of scope - should not call
    // deleter.
    {
        CountingArrPtr ptr(nullptr);
    }

    EXPECT_EQ(0, destroy_count, "");

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

    destroy_count = 0;

    // Test operator bool
    {
        CountingArrPtr foo(new int[1]);
        EXPECT_TRUE(static_cast<bool>(foo), "");

        foo.reset();
        EXPECT_EQ(1, destroy_count, "");
        EXPECT_FALSE(static_cast<bool>(foo), "");
    }

    destroy_count = 0;

    printf("all tests passed\n");
    END_TEST;
}
