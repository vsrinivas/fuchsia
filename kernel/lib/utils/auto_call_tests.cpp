// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <utils/auto_call.h>

#include <app/tests.h>
#include <unittest.h>

static volatile int test_func_count;

__NO_INLINE static void test_func()
{
    test_func_count++;
}

static bool auto_call_test(void* context)
{
//    extern int foo();
//    int a;
//
//    auto ac = MakeAutoCall([&](){ a = 1; });
//    auto ac2 = MakeAutoCall(foo);
//
//    auto func = [&](){ a = 2; };
//    AutoCall<decltype(bleh)> ac3(func);
//    AutoCall<decltype(&foo)> ac4(&foo);
//
//    // abort the call
//    ac2.cancel();

    BEGIN_TEST;

    // mark as volatile to make sure it generates code
    volatile int a;

    // call a lambda
    {
        a = 0;
        auto ac = utils::MakeAutoCall([&](){ a++; });
        EXPECT_EQ(a, 0, "autocall hasn't run");
    }
    EXPECT_EQ(a, 1, "autocall has run");

    {
        a = 0;
        auto ac = utils::MakeAutoCall([&](){ a++; });
        EXPECT_EQ(a, 0, "autocall hasn't run");

        ac.cancel();
        EXPECT_EQ(a, 0, "autocall still hasn't run");

        ac.call();
        EXPECT_EQ(a, 0, "autocall still hasn't run");
    }
    EXPECT_EQ(a, 0, "autocall still hasn't run");

    {
        a = 0;
        auto ac = utils::MakeAutoCall([&](){ a++; });
        EXPECT_EQ(a, 0, "autocall hasn't run");

        ac.call();
        EXPECT_EQ(a, 1, "autocall should have run\n");

        ac.cancel();
        EXPECT_EQ(a, 1, "autocall ran only once\n");
    }
    EXPECT_EQ(a, 1, "autocall ran only once\n");

    // call a regular function
    {
        test_func_count = 0;
        auto ac = utils::MakeAutoCall(&test_func);
        EXPECT_EQ(test_func_count, 0, "autocall hasn't run");
    }
    EXPECT_EQ(test_func_count, 1, "autocall has run");

    END_TEST;
}

STATIC_UNITTEST_START_TESTCASE(auto_call_tests)
STATIC_UNITTEST("Auto call test", auto_call_test)
STATIC_UNITTEST_END_TESTCASE(auto_call_tests, "autocalltests", "Auto call test", NULL, NULL);
