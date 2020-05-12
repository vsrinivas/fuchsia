// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <unittest/unittest.h>

#include "threads_impl.h"

static bool stdio_handle_to_tid_mapping(void) {
  BEGIN_TEST;

  // Basic expectations.
  ASSERT_EQ(__thread_handle_to_filelock_tid(0b0011), 0, "");
  ASSERT_EQ(__thread_handle_to_filelock_tid(0b0111), 1, "");
  ASSERT_EQ(__thread_handle_to_filelock_tid(0x123f), 0x48f, "");
  ASSERT_EQ(__thread_handle_to_filelock_tid(0x80000000), 0x20000000, "");
  ASSERT_EQ(__thread_handle_to_filelock_tid(0xffffffff), 0x3fffffff, "");
  ASSERT_EQ(__thread_handle_to_filelock_tid(0xffffffff), 0x3fffffff, "");

  zx_handle_t last_h0 = 0;
  for (zx_handle_t h0 = ZX_HANDLE_FIXED_BITS_MASK; h0 > last_h0;
       last_h0 = h0, h0 += ZX_HANDLE_FIXED_BITS_MASK + 1) {
    // Ensure no handles are ever mapped to negative.
    ASSERT_GE(__thread_handle_to_filelock_tid(h0), 0, "pid_t must be >= 0");
  }

  END_TEST;
}

BEGIN_TEST_CASE(musl_tests)
RUN_TEST(stdio_handle_to_tid_mapping);
END_TEST_CASE(musl_tests)
