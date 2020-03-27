// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_FIT_UNITTEST_UTILS_H_
#define ZIRCON_SYSTEM_UTEST_FIT_UNITTEST_UTILS_H_

#include <stdlib.h>

#include <unittest/unittest.h>

// Asserts that a condition is true.  If false, prints an error then
// aborts the test run.  Use only when |current_test_info| is not in scope.
//
// Note: The <unittest.h> ASSERT_* and EXPECT_* macros only work within
// the body of a test function or a test helper since they assume that
// |current_test_info| is in scope and that the function returns bool
// to indicate success or failure.  This isn't always practical for the
// tests in this library.  Crashing the process when a test fails isn't
// great but it's better than not checking the condition.
#define ASSERT_CRITICAL(x)                                                                         \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      unittest_printf_critical("ASSERT_CRITICAL FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)

#endif  // ZIRCON_SYSTEM_UTEST_FIT_UNITTEST_UTILS_H_
