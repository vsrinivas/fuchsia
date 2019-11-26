// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <fbl/alloc_checker.h>
#include <zxtest/zxtest.h>

// These test verify behavior of AllocChecker that cannot easily be verified by its main in-kernel
// test suite, //zircon/kernel/tests/alloc_checker_tests.cc.  Prefer to add tests there instead.

namespace {

#if ZX_DEBUG_ASSERT_IMPLEMENTED

TEST(AllocCheckerTests, PanicIfDestroyedWhenArmed) {
  ASSERT_DEATH(
      []() {
        fbl::AllocChecker ac;
        ac.arm(1, false);
      },
      "AllocChecker should have panicked because it was destroyed while armed");
}

TEST(AllocCheckerTests, PanicIfReusedWhenArmed) {
  fbl::AllocChecker ac;
  ac.arm(1, false);
  ASSERT_DEATH([&ac]() { ac.arm(1, false); },
               "AllocChecker should have panicked because it was used while armed");
  ASSERT_FALSE(ac.check());
}

#else

TEST(AllocCheckerTests, DontPanicIfDestroyedWhenArmed) {
  fbl::AllocChecker ac;
  ac.arm(1, false);
}

TEST(AllocCheckerTests, DontPanicIfReusedWhenArmed) {
  fbl::AllocChecker ac;
  ac.arm(1, false);
  ac.arm(1, false);
  ASSERT_FALSE(ac.check());
}

#endif

}  // namespace
