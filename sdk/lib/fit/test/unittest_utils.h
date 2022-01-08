// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_TEST_UNITTEST_UTILS_H_
#define LIB_FIT_TEST_UNITTEST_UTILS_H_

#include <stdlib.h>

#include <zxtest/zxtest.h>

// Asserts that a condition is true.  If false, prints an error then
// aborts the test run.
//
// This is for use when we don't want the test to continue running after a
// failure.  zxtest's ASSERT_*() macros do "return;" on failure, which
// doesn't work in functions with non-void return types, and it continues
// running if the calling function doesn't check for failure with something
// like ASSERT_NO_FATAL_FAILURE().
//
// Functions defined using zxtest's TEST() macro should use zxtest's
// ASSERT_*() and EXPECT_*() assertions instead of ASSERT_CRITICAL().
#define ASSERT_CRITICAL(x)                                                       \
  do {                                                                           \
    if (!(x)) {                                                                  \
      printf("ASSERT_CRITICAL FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
      abort();                                                                   \
    }                                                                            \
  } while (0)

#endif  // LIB_FIT_TEST_UNITTEST_UTILS_H_
