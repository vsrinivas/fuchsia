// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <zxtest/zxtest.h>

#include "helper.h"

static int called = 0;

TEST(CTest, AutoRegister) { called = 1; }

static void Verify(void) {
  // This variable should exist if everything works well. Else we would reference an
  // inexistent symbol and get a compile error.
  __attribute__((unused)) zxtest_test_ref_t test_ref = _ZXTEST_TEST_REF(CTest, AutoRegister);
  ZX_ASSERT_MSG(called != 0, "TEST registered test did not run.");
  called = 0;
}

static void Add(void) __attribute__((constructor));
static void Add(void) { zxtest_add_check_function(&Verify); }
