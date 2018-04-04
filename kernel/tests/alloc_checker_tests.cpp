// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <lib/unittest/unittest.h>

static bool alloc_checker_ctor() {
    BEGIN_TEST;

    {
        fbl::AllocChecker ac;
    }

    {
        fbl::AllocChecker ac;
        // In some error cases for "new", some versions of C++ say that the
        // allocation function (i.e. the "operator new" overload) is not
        // called, and "new" returns NULL.  So check that ac.check()
        // indicates failure even when AllocChecker's "operator new" wasn't
        // called.
        EXPECT_FALSE(ac.check(), "");
    }

    END_TEST;
}

static bool alloc_checker_basic() {
    BEGIN_TEST;

    fbl::AllocChecker ac;
    ac.arm(8u, true);
    EXPECT_TRUE(ac.check(), "");

    ac.arm(16u, false);
    EXPECT_FALSE(ac.check(), "");

    // Allocating zero bytes, allways succeeds.
    ac.arm(0u, false);
    EXPECT_TRUE(ac.check(), "");

    END_TEST;
}

static bool alloc_checker_panic() {
    BEGIN_TEST;
// Enable any of the blocks below to test the possible panics.

#if 0
    // Arm but not check should panic (true).
    {
        fbl::AllocChecker ac;
        ac.arm(24u, true);
    }
#endif

#if 0
    // Arm but not check should panic (false).
    {
        fbl::AllocChecker ac;
        ac.arm(24u, false);
    }
#endif

#if 0
    // Arming twice without a check should panic.
    {
        fbl::AllocChecker ac;
        ac.arm(24u, true);
        ac.arm(18u, true);
    }
#endif

    END_TEST;
}

struct StructWithCtor {
    char field = 5;
};
static_assert(sizeof(StructWithCtor) == 1, "");

static bool alloc_checker_new() {
    BEGIN_TEST;

    const int kCount = 128;
    fbl::AllocChecker ac;
    fbl::unique_ptr<StructWithCtor[]> arr(new (&ac) StructWithCtor[kCount]);
    EXPECT_EQ(ac.check(), true, "");

    // Check that the constructor got run.
    for (int i = 0; i < kCount; ++i)
        EXPECT_EQ(arr[i].field, 5, "");

    END_TEST;
}

static bool alloc_checker_new_fails() {
    BEGIN_TEST;

    // malloc(size_t_max) should fail but currently does not (see
    // ZX-1760), so use large_size instead, because malloc(large_size)
    // does fail.
    size_t size_t_max = ~(size_t)0;
    size_t large_size = size_t_max >> 1;

    // Use a type with a constructor to check that we are not attempting to
    // run the constructor when the allocation fails.
    fbl::AllocChecker ac;
    EXPECT_EQ(new (&ac) StructWithCtor[large_size], nullptr, "");
    EXPECT_EQ(ac.check(), false, "");

    END_TEST;
}

struct LargeStruct {
    uint8_t array[0x1000];
};

static bool test_array_size_overflow_check() {
    BEGIN_TEST;

    size_t size_t_max = ~(size_t)0;
    size_t count = size_t_max / (sizeof(LargeStruct) / 0x10);
    fbl::AllocChecker ac;
    // For this array allocation, the C++ compiler will generate a
    // count*sizeof(LargeStruct) multiplication.  If this overflows, it
    // passes size_t_max to the "operator new()" function.  Check that this
    // allocation fails properly.
    //
    // |count| should be non-const (and non-constexpr) otherwise the
    // compiler may complain at compile time that the program is ill-formed
    // (possibly depending on C++ version).
    //
    // This also requires that AllocChecker's "operator new" override be
    // declared as "noexcept" (which AllocChecker relies on anyway),
    // otherwise the compiler might generate code that raises an exception
    // (again, depending on C++ version).
    EXPECT_EQ(new (&ac) LargeStruct[count], nullptr, "");
    EXPECT_EQ(ac.check(), false, "");

    END_TEST;
}

static bool test_negative_array_size() {
    BEGIN_TEST;

    fbl::AllocChecker ac;

    // Test passing a signed, negative array size.  This should fail
    // safely.
    //
    // C++14 and C++17 say that this size is "erroneous" because "the
    // expression is of non-class type and its value before converting to
    // std::size_t is less than zero".
    //
    // |count| should be non-const (and non-constexpr) otherwise the
    // compiler may complain at compile time that the program is ill-formed
    // (possibly depending on C++ version).
    int count = -1;
    EXPECT_EQ(new (&ac) char[count], nullptr, "");
    EXPECT_EQ(ac.check(), false, "");

    END_TEST;
}

UNITTEST_START_TESTCASE(alloc_checker)
UNITTEST("alloc checker ctor & dtor", alloc_checker_ctor)
UNITTEST("alloc checker basic", alloc_checker_basic)
UNITTEST("alloc checker panic", alloc_checker_panic)
UNITTEST("alloc checker new", alloc_checker_new)
UNITTEST("alloc checker new fails", alloc_checker_new_fails)
UNITTEST("test array size overflow check", test_array_size_overflow_check)
UNITTEST("test negative array size", test_negative_array_size)
UNITTEST_END_TESTCASE(alloc_checker, "alloc_cpp", "Tests of the C++ AllocChecker");
