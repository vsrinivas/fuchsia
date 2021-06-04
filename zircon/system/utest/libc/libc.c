// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include "threads_impl.h"

#define BEGIN_TEST

#define END_TEST return true

#define ASSERT_EQ(lhs, rhs, msg)          \
  if ((lhs) != (rhs)) {                   \
    fprintf(stderr, "failed: %s\n", msg); \
    return false;                         \
  }

#define ASSERT_NE(lhs, rhs, msg)          \
  if ((lhs) == (rhs)) {                   \
    fprintf(stderr, "failed: %s\n", msg); \
    return false;                         \
  }

#define ASSERT_GE(lhs, rhs, msg)          \
  if ((lhs) < (rhs)) {                    \
    fprintf(stderr, "failed: %s\n", msg); \
    return false;                         \
  }

#define BEGIN_TEST_CASE(name) \
  int main(int argc, char** argv) { \

#define END_TEST_CASE(name) \
    return 0;                       \
  }

#define RUN_TEST(function_name)             \
  fprintf(stderr, "%s\n", #function_name); \
  if (!function_name()) {                   \
    return 1;                               \
  }

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

static bool strptime_parse_percent_p(void) {
  BEGIN_TEST;

  struct tm tm = {0};
  const char* input = "AM";
  // Regression test for https://fxrev.dev/539032; parse pointer wasn't advanced
  // past the parse of %p, so the return value was incorrect.
  const char* result = strptime(input, "%p", &tm);
  ASSERT_NE(result, input, "didn't advance");
  ASSERT_NE(result, NULL, "null");

  END_TEST;
}

BEGIN_TEST_CASE(musl_tests)
RUN_TEST(stdio_handle_to_tid_mapping);
RUN_TEST(strptime_parse_percent_p);
END_TEST_CASE(musl_tests)
