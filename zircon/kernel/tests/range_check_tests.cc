// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <kernel/range_check.h>

static bool test_range_check() {
  BEGIN_TEST;

  EXPECT_TRUE(InRange(0u, 1024u, 4096u));       // [0, 1024) is within [0, 4096)
  EXPECT_TRUE(InRange(0u, 1024u, 0u, 4096u));   // [0, 1024) is within [0, 4096)
  EXPECT_FALSE(InRange(0u, 1024u, 1u, 4096u));  // [0, 1024) is not within [1, 4096)
  EXPECT_TRUE(InRange(0u, 1024u, 0u, 1024u));   // [0, 1024) is within [0, 1024)
  EXPECT_FALSE(InRange(0u, 1024u, 0u, 1023u));  // [0, 1024) is not within [0, 1023)

  // offset < min tests; (underflow)
  EXPECT_FALSE(InRange(32768u, 1024u, 524288u, 1048576u));

  EXPECT_FALSE(InRange(4000u, 1000u, 4500u, 5500u));  // Right overlap
  EXPECT_FALSE(InRange(5000u, 1000u, 4500u, 5500u));  // Left overlap
  EXPECT_FALSE(InRange(4000u, 2000u, 4500u, 5500u));  // Full overlap

  END_TEST;
}

UNITTEST_START_TESTCASE(range_check_tests)
UNITTEST("basic test of range checks", test_range_check)
UNITTEST_END_TESTCASE(range_check_tests, "range_check_tests", "Tests of range_check.h")
