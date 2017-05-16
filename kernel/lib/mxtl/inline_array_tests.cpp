// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/inline_array.h>

#include <stddef.h>
#include <mxalloc/new.h>
#include <unittest.h>

namespace {

struct TestType {
    TestType() {
        ctor_run_count++;
    }

    ~TestType() {
        dtor_run_count++;
    }

    static void ResetRunCounts() {
        ctor_run_count = 0u;
        dtor_run_count = 0u;
    }

    static size_t ctor_run_count;
    static size_t dtor_run_count;
};

size_t TestType::ctor_run_count = 0u;
size_t TestType::dtor_run_count = 0u;

bool inline_test(void* unused) {
    BEGIN_TEST;

    for (size_t sz = 0u; sz <= 3u; sz++) {
        TestType::ResetRunCounts();
        {
            AllocChecker ac;
            mxtl::InlineArray<TestType, 3u> ia(&ac, sz);
            EXPECT_TRUE(ac.check(), "");
        }
        EXPECT_EQ(TestType::ctor_run_count, sz, "");
        EXPECT_EQ(TestType::dtor_run_count, sz, "");
    }

    END_TEST;
}

bool non_inline_test(void* unused) {
    static const size_t test_sizes[] = { 4u, 5u, 6u, 10u, 100u, 1000u };

    BEGIN_TEST;

    for (size_t i = 0u; i < countof(test_sizes); i++) {
        size_t sz = test_sizes[i];

        TestType::ResetRunCounts();
        {
            AllocChecker ac;
            mxtl::InlineArray<TestType, 3u> ia(&ac, sz);
            EXPECT_TRUE(ac.check(), "");
        }
        EXPECT_EQ(TestType::ctor_run_count, sz, "");
        EXPECT_EQ(TestType::dtor_run_count, sz, "");
    }

    END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(inline_array_tests)
UNITTEST("inline test", inline_test)
UNITTEST("non-inline test", non_inline_test)
UNITTEST_END_TESTCASE(inline_array_tests, "inlinearraytests", "Inline array test", nullptr, nullptr);
