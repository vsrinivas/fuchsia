// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <atomic>

namespace {

// Increments |count| (to ensure we actually were called) then crashes.
void Crash(void* count) {
    (*static_cast<std::atomic_int*>(count))++;

    volatile int* p = nullptr;
    *p = 0;
}

// Increments |count| (to ensure we actually were called) then returns.
void NoOp(void* count) {
    (*static_cast<std::atomic_int*>(count))++;
}

bool assert_death_test() {
    BEGIN_TEST;

    std::atomic_int count = 0;
    ASSERT_DEATH(&Crash, &count, "Crash() should have crashed");
    EXPECT_EQ(1, count.load());

    END_TEST;
}

bool assert_no_death_test() {
    BEGIN_TEST;

    std::atomic_int count = 0;
    ASSERT_NO_DEATH(&NoOp, &count, "NoOp() should not have crashed");
    EXPECT_EQ(1, count.load());

    END_TEST;
}

bool repeated_death_test() {
    BEGIN_TEST;

    std::atomic_int count = 0;
    ASSERT_DEATH(&Crash, &count, "Crash() [1] should have crashed");
    ASSERT_NO_DEATH(&NoOp, &count, "NoOp() [2] should not have crashed");
    ASSERT_NO_DEATH(&NoOp, &count, "NoOp() [3] should not have crashed");
    ASSERT_DEATH(&Crash, &count, "Crash() [4] should have crashed");
    EXPECT_EQ(4, count.load());

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(unittest_tests)
RUN_TEST(assert_death_test);
RUN_TEST(assert_no_death_test);
RUN_TEST(repeated_death_test);
END_TEST_CASE(unittest_tests)
