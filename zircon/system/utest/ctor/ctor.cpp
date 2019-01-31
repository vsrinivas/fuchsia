// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <sched.h>
#include <threads.h>

#include "dso-ctor/dso-ctor.h"

namespace {

bool global_ctor_ran;

static struct Global {
    Global() { global_ctor_ran = true; }
    ~Global() {
        // This is just some random nonempty thing that the compiler
        // can definitely never decide to optimize away.  We can't
        // easily test that the destructor got run, but we can ensure
        // that using a static destructor compiles and links correctly.
        sched_yield();
    }
} global;

bool check_ctor() {
    BEGIN_TEST;
    EXPECT_TRUE(global_ctor_ran, "global constuctor didn't run!");
    END_TEST;
}

int my_static = 23;

bool check_initializer() {
    BEGIN_TEST;
    EXPECT_EQ(my_static, 23, "static initializer didn't run!");
    END_TEST;
}

bool tlocal_ctor_ran, tlocal_dtor_ran;
thread_local ThreadLocal<&tlocal_ctor_ran, &tlocal_dtor_ran> tlocal;

int do_thread_local_dtor_test(void*) {
    BEGIN_HELPER;
    EXPECT_TRUE(decltype(tlocal)::check_before_reference());
    tlocal.flag = true;
    EXPECT_TRUE(decltype(tlocal)::check_after_reference());
    EXPECT_TRUE(check_dso_tlocal_in_thread());
    END_HELPER;
}

bool check_thread_local_ctor_dtor() {
    BEGIN_TEST;
    thrd_t th;
    ASSERT_EQ(thrd_create(&th, &do_thread_local_dtor_test, nullptr),
              thrd_success);
    int retval = -1;
    EXPECT_EQ(thrd_join(th, &retval), thrd_success);
    EXPECT_TRUE(static_cast<bool>(retval));
    EXPECT_TRUE(decltype(tlocal)::check_after_join());
    EXPECT_TRUE(check_dso_tlocal_after_join());
    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(ctors)
RUN_TEST(check_ctor)
RUN_TEST(check_initializer)
RUN_TEST(check_dso_ctor)
RUN_TEST(check_thread_local_ctor_dtor)
END_TEST_CASE(ctors)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
