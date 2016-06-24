// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

int main(void) {
  return unittest_run_all_tests() ? 0 : -1;
}
