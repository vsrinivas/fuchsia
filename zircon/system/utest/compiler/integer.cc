// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include <zxtest/zxtest.h>

namespace {

// Global to prevent "unused variable" compiler error, volatile to make sure
// the compiler doesn't optimize out any operations.
volatile int c = 0;

TEST(IntegerTest, NormalMath) {
    volatile int a = 5;
    volatile int b = 6;
    c = a + b;
    EXPECT_EQ(11, c);
}

TEST(IntegerTest, SignedOverflow) {
    ASSERT_DEATH([] {
        volatile int a = std::numeric_limits<int>::max();
        volatile int b = 6;
        c = a + b;  // crash occurs here
    });
}

TEST(IntegerTest, SignedUnderflow) {
    ASSERT_DEATH([] {
        volatile int a = std::numeric_limits<int>::min();
        volatile int b = -6;
        c = a + b;  // crash occurs here
    });
}

TEST(IntegerTest, DivideByZero) {
    ASSERT_DEATH([] {
        volatile int a = 5;
        volatile int b = 0;
        c = a / b;  // crash occurs here
    });
}

}  // namespace
