// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sched.h>
#include <threads.h>

#include <zxtest/zxtest.h>

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

TEST(Ctors, check_ctor) { EXPECT_TRUE(global_ctor_ran, "global constuctor didn't run!"); }

TEST(Ctors, check_dso_ctor) { check_dso_ctor(); }

int my_static = 23;

TEST(Ctors, check_initializer) { EXPECT_EQ(my_static, 23, "static initializer didn't run!"); }

bool tlocal_ctor_ran, tlocal_dtor_ran;
thread_local ThreadLocal<&tlocal_ctor_ran, &tlocal_dtor_ran> tlocal;

int do_thread_local_dtor_test(void*) {
  decltype(tlocal)::check_before_reference();
  tlocal.flag = true;
  decltype(tlocal)::check_after_reference();
  check_dso_tlocal_in_thread();
  return 1;
}

TEST(Ctors, check_thread_local_ctor_dtor) {
  thrd_t th;
  ASSERT_EQ(thrd_create(&th, &do_thread_local_dtor_test, nullptr), thrd_success);
  int retval = -1;
  EXPECT_EQ(thrd_join(th, &retval), thrd_success);
  EXPECT_TRUE(static_cast<bool>(retval));
  decltype(tlocal)::check_after_join();
  check_dso_tlocal_after_join();
}

}  // namespace
