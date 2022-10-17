// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This is a small test to ensure that the kernel can see the necessary
// headers used by libzxc.

#include <lib/unittest/unittest.h>
#include <lib/zx/status.h>
#include <stddef.h>
#include <string.h>

namespace {

zx::result<size_t> StringLength(const char* string) {
  if (string == nullptr) {
    return zx::error_result(ZX_ERR_INVALID_ARGS);
  }
  return zx::ok(strlen(string));
}

bool result_test() {
  BEGIN_TEST;

  {
    auto result = StringLength(nullptr);
    EXPECT_TRUE(result.is_error());
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, result.error_value());
  }

  {
    auto result = StringLength("12345");
    EXPECT_FALSE(result.is_error());
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(ZX_OK, result.status_value());
    EXPECT_EQ(5u, result.value());
    EXPECT_TRUE(5u == result);
  }

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(result_tests)
UNITTEST("result", result_test)
UNITTEST_END_TESTCASE(result_tests, "resulttests", "zxc::result tests")
