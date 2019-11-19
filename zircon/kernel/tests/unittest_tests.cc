// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

namespace {

bool GetMessageTest() {
  BEGIN_TEST;

  ASSERT_TRUE(strcmp("", unittest_get_msg()) == 0);
  ASSERT_TRUE(strcmp("foo", unittest_get_msg("foo")) == 0, "custom message");

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(unittest_tests)
UNITTEST("get_message", GetMessageTest)
UNITTEST_END_TESTCASE(unittest_tests, "unittest", "unittest tests")
