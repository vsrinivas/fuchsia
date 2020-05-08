// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// Functions for unit tests.  See lib/unittest/include/unittest.h for usage.

#include <lib/unittest/unittest.h>
#include <stdio.h>

#include <pretty/hexdump.h>

int unittest_printf(const char* format, ...) {
  int ret = 0;

  va_list argp;
  va_start(argp, format);
  ret = vprintf(format, argp);
  va_end(argp);

  return ret;
}

bool unittest_expect_bytes(const uint8_t* expected, const char* expected_name,
                           const uint8_t* actual, const char* actual_name, size_t len,
                           const char* msg, const char* func, int line, bool expect_eq) {
  if (!memcmp(expected, actual, len) != expect_eq) {
    unittest_printf(UNITTEST_FAIL_TRACEF_FORMAT "%s:\n%s %s %s, but %s!\n", func, line, msg,
                    expected_name, expect_eq ? "does not match" : "matches", actual_name,
                    expect_eq ? "should" : "should not");

    unittest_printf("expected (%s)\n", expected_name);
    hexdump8_ex(expected, len, (uint64_t)((uintptr_t)expected));
    unittest_printf("actual (%s)\n", actual_name);
    hexdump8_ex(actual, len, (uint64_t)((uintptr_t)actual));

    return false;
  }
  return true;
}
