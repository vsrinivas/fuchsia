// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <unittest/unittest.h>

static bool sysconf_test(void) {
  BEGIN_TEST;

  long rv;
  rv = sysconf(_SC_NPROCESSORS_CONF);
  EXPECT_GE(rv, 1, "wrong number of cpus configured");
  rv = sysconf(_SC_NPROCESSORS_ONLN);
  EXPECT_GE(rv, 1, "wrong number of cpus currently online");
  // test on invalid input
  rv = sysconf(-1);
  EXPECT_EQ(rv, -1, "wrong return value on invalid input");

  END_TEST;
}

BEGIN_TEST_CASE(sysconf_tests)
RUN_TEST(sysconf_test)
END_TEST_CASE(sysconf_tests)

int main(int argc, char** argv) {
  return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
