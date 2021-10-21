// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/backtrace.h>
#include <lib/unittest/unittest.h>
#include <string-file.h>

#include <kernel/thread.h>
#include <ktl/span.h>

static bool VectorOpsTest() {
  BEGIN_TEST;

  Backtrace bt;
  ASSERT_EQ(0u, bt.size());
  bt.reset();
  ASSERT_EQ(0u, bt.size());

  for (size_t x = 0; x < Backtrace::kMaxSize; ++x) {
    bt.push_back(x);
    ASSERT_EQ(x, bt.size() - 1);
  }
  bt.reset();
  ASSERT_EQ(0u, bt.size());

  ASSERT_EQ(0u, bt.size());

  END_TEST;
}

static bool PrintTest() {
  BEGIN_TEST;

  Backtrace bt;
  bt.push_back(0xffffffff76543210);

  char buffer[1024];
  StringFile file(ktl::span(buffer, sizeof(buffer)));
  bt.Print(&file);
  EXPECT_NE(ktl::string_view::npos, file.as_string_view().find("zx_system_get_version_string"));
  EXPECT_NE(ktl::string_view::npos, file.as_string_view().find("{{{bt:0:0xffffffff76543210}"));

  END_TEST;
}

static bool PrintWithoutVersionTest() {
  BEGIN_TEST;

  Backtrace bt;
  bt.push_back(0xffffffff76543210);

  char buffer[1024];
  StringFile file(ktl::span(buffer, sizeof(buffer)));
  bt.PrintWithoutVersion(&file);
  EXPECT_EQ(ktl::string_view::npos, file.as_string_view().find("zx_system_get_version_string"));
  EXPECT_NE(ktl::string_view::npos, file.as_string_view().find("{{{bt:0:0xffffffff76543210}"));

  END_TEST;
}

UNITTEST_START_TESTCASE(backtrace_tests)
UNITTEST("VectorOps", VectorOpsTest)
UNITTEST("Print", PrintTest)
UNITTEST("PrintWithoutVersion", PrintWithoutVersionTest)
UNITTEST_END_TESTCASE(backtrace_tests, "backtrace", "backtrace tests")
