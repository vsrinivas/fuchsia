// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <stdbool.h>
#include <stdint.h>

static bool global_ctor_ran;

static struct Global {
    Global() { global_ctor_ran = true; }
} global;

bool check_ctor() {
    BEGIN_TEST;
    EXPECT_TRUE(global_ctor_ran, "global constuctor didn't run!");
    END_TEST;
}

static int my_static = 23;

bool check_initializer() {
    BEGIN_TEST;
    EXPECT_EQ(my_static, 23, "static initializer didn't run!");
    END_TEST;
}

BEGIN_TEST_CASE(ctors)
RUN_TEST(check_ctor)
RUN_TEST(check_initializer)
END_TEST_CASE(ctors)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
