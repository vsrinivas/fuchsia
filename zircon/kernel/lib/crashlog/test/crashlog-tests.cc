// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crashlog.h>
#include <lib/crashlog/panic_buffer.h>
#include <lib/unittest/unittest.h>

#include <ktl/string_view.h>
#include <ktl/unique_ptr.h>

namespace {

constexpr size_t kLargeBufferSize = 64 * 1024;
constexpr size_t kTooSmallBufferSize = 64;

bool BasicTest() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  auto buffer = ktl::make_unique<char[]>(&ac, kLargeBufferSize);
  ASSERT_TRUE(ac.check());

  size_t len = crashlog_to_string(buffer.get(), kLargeBufferSize, ZirconCrashReason::NoCrash);
  EXPECT_LE(len, kLargeBufferSize);
  EXPECT_GT(len, 0u);

  ktl::string_view text{buffer.get(), len};
  EXPECT_TRUE(text.ends_with('\n'));
  EXPECT_TRUE(text.find("BACKTRACE"sv) != ktl::string_view::npos);

  END_TEST;
}

bool OomTest() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  auto buffer = ktl::make_unique<char[]>(&ac, kLargeBufferSize);
  ASSERT_TRUE(ac.check());

  size_t len = crashlog_to_string(buffer.get(), kLargeBufferSize, ZirconCrashReason::Oom);
  EXPECT_LE(len, kLargeBufferSize);
  EXPECT_GT(len, 0u);

  // OOM case should not include the full dump.
  ktl::string_view text{buffer.get(), len};
  EXPECT_TRUE(text.ends_with('\n'));
  EXPECT_TRUE(text.find("BACKTRACE"sv) == ktl::string_view::npos);

  END_TEST;
}

bool TruncationTest() {
  BEGIN_TEST;

  constexpr char kCanaryValue = '\xa5';
  char buffer[kTooSmallBufferSize + 1];

  buffer[sizeof(buffer) - 1] = kCanaryValue;

  size_t len = crashlog_to_string(buffer, sizeof(buffer) - 1, ZirconCrashReason::NoCrash);
  EXPECT_LT(len, sizeof(buffer));
  EXPECT_GT(len, 0u);

  EXPECT_EQ(buffer[sizeof(buffer) - 1], kCanaryValue);

  END_TEST;
}

bool PanicBufferTest() {
  BEGIN_TEST;

  PanicBuffer b;
  ASSERT_EQ(0u, strlen(b.c_str()));

  const char kMsg[] = "hello";
  b.Append(kMsg);
  ASSERT_FALSE(strcmp(b.c_str(), kMsg));

  // Grossly over append.
  for (unsigned i = 0; i < PanicBuffer::kMaxSize; ++i) {
    b.Append(kMsg);
  }
  ASSERT_EQ(PanicBuffer::kMaxSize - 1, strlen(b.c_str()));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(crashlog_tests)
UNITTEST("basic", BasicTest)
UNITTEST("oom", OomTest)
UNITTEST("truncation", TruncationTest)
UNITTEST("panic_buffer", PanicBufferTest)
UNITTEST_END_TESTCASE(crashlog_tests, "crashlog", "crashlog tests")
