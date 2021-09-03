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

extern FILE gSerialFile;
namespace {

constexpr size_t kLargeBufferSize = 8 * 1024;
constexpr size_t kTooSmallBufferSize = 64;

bool NoCrashTest() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  auto buffer = ktl::make_unique<char[]>(&ac, kLargeBufferSize);
  ASSERT_TRUE(ac.check());

  // NoCrash should currently produce no crash payload at all.
  size_t len = crashlog_to_string({buffer.get(), kLargeBufferSize}, ZirconCrashReason::NoCrash);
  EXPECT_EQ(0u, len);

  END_TEST;
}

bool OomRootJobTest() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  auto buffer = ktl::make_unique<char[]>(&ac, kLargeBufferSize);
  ASSERT_TRUE(ac.check());

  // OOM and UserspaceRootJobTermination reasons should produce all of the
  // sections, except for the debugging context and panic buffer.
  constexpr ktl::array kReasons = {
      ZirconCrashReason::Oom,
      ZirconCrashReason::UserspaceRootJobTermination,
  };

  for (auto reason : kReasons) {
    memset(buffer.get(), 0, kLargeBufferSize);
    size_t len = crashlog_to_string({buffer.get(), kLargeBufferSize}, reason);
    ASSERT_LE(len, kLargeBufferSize);
    ASSERT_GT(len, 0u);

    ktl::string_view text{buffer.get(), len};

    // Banner.
    EXPECT_TRUE(text.find("ZIRCON REBOOT REASON"sv) != ktl::string_view::npos);
    EXPECT_TRUE(text.find("UPTIME"sv) != ktl::string_view::npos);
    EXPECT_TRUE(text.find("VERSION"sv) != ktl::string_view::npos);

    // No debug info.
    EXPECT_FALSE(text.find("{{{reset}}}"sv) != ktl::string_view::npos);
    EXPECT_FALSE(text.find("REGISTERS"sv) != ktl::string_view::npos);
    EXPECT_FALSE(text.find("BACKTRACE"sv) != ktl::string_view::npos);

    // Critical counters.
    EXPECT_TRUE(text.find("counters: "sv) != ktl::string_view::npos);

    // No panic buffer.
    EXPECT_FALSE(text.find("panic buffer: "sv) != ktl::string_view::npos);

    // Debug log.
    EXPECT_TRUE(text.find("--- BEGIN DLOG DUMP ---"sv) != ktl::string_view::npos);
    EXPECT_TRUE(text.find("--- END DLOG DUMP ---"sv) != ktl::string_view::npos);
  }

  END_TEST;
}

bool PanicWdtTest() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  auto buffer = ktl::make_unique<char[]>(&ac, kLargeBufferSize);
  ASSERT_TRUE(ac.check());

  // Panic and SoftwareWatchdog reasons should produce all of the sections.
  constexpr ktl::array kReasons = {ZirconCrashReason::Panic, ZirconCrashReason::SoftwareWatchdog};

  for (auto reason : kReasons) {
    memset(buffer.get(), 0, kLargeBufferSize);
    size_t len = crashlog_to_string({buffer.get(), kLargeBufferSize}, reason);
    ASSERT_LE(len, kLargeBufferSize);
    ASSERT_GT(len, 0u);

    ktl::string_view text{buffer.get(), len};

    // Banner.
    EXPECT_TRUE(text.find("ZIRCON REBOOT REASON"sv) != ktl::string_view::npos);
    EXPECT_TRUE(text.find("UPTIME"sv) != ktl::string_view::npos);
    EXPECT_TRUE(text.find("VERSION"sv) != ktl::string_view::npos);

    // Debug info.
    EXPECT_TRUE(text.find("{{{reset}}}"sv) != ktl::string_view::npos);
    EXPECT_TRUE(text.find("REGISTERS"sv) != ktl::string_view::npos);
    EXPECT_TRUE(text.find("BACKTRACE"sv) != ktl::string_view::npos);

    // Critical counters.
    EXPECT_TRUE(text.find("counters: "sv) != ktl::string_view::npos);

    // Panic buffer.
    EXPECT_TRUE(text.find("panic buffer: "sv) != ktl::string_view::npos);

    // Debug log.
    EXPECT_TRUE(text.find("--- BEGIN DLOG DUMP ---"sv) != ktl::string_view::npos);
    EXPECT_TRUE(text.find("--- END DLOG DUMP ---"sv) != ktl::string_view::npos);
  }

  END_TEST;
}

bool UnknownTest() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  auto buffer = ktl::make_unique<char[]>(&ac, kLargeBufferSize);
  ASSERT_TRUE(ac.check());

  // Unknown reasons should include just the banner.
  constexpr ktl::array kReasons = {
      ZirconCrashReason::Unknown,
      static_cast<ZirconCrashReason>(0xbaadf00d),
  };

  for (auto reason : kReasons) {
    memset(buffer.get(), 0, kLargeBufferSize);
    size_t len = crashlog_to_string({buffer.get(), kLargeBufferSize}, reason);
    ASSERT_LE(len, kLargeBufferSize);
    ASSERT_GT(len, 0u);

    ktl::string_view text{buffer.get(), len};

    // Banner.
    EXPECT_TRUE(text.find("ZIRCON REBOOT REASON"sv) != ktl::string_view::npos);
    EXPECT_TRUE(text.find("UPTIME"sv) != ktl::string_view::npos);
    EXPECT_TRUE(text.find("VERSION"sv) != ktl::string_view::npos);

    // No debug info.
    EXPECT_FALSE(text.find("{{{reset}}}"sv) != ktl::string_view::npos);
    EXPECT_FALSE(text.find("REGISTERS"sv) != ktl::string_view::npos);
    EXPECT_FALSE(text.find("BACKTRACE"sv) != ktl::string_view::npos);

    // No critical counters.
    EXPECT_FALSE(text.find("counters: "sv) != ktl::string_view::npos);

    // No panic buffer.
    EXPECT_FALSE(text.find("panic buffer: "sv) != ktl::string_view::npos);

    // No debug log.
    EXPECT_FALSE(text.find("--- BEGIN DLOG DUMP ---"sv) != ktl::string_view::npos);
    EXPECT_FALSE(text.find("--- END DLOG DUMP ---"sv) != ktl::string_view::npos);
  }

  END_TEST;
}

bool TruncationTest() {
  BEGIN_TEST;

  constexpr char kCanaryValue = '\xa5';
  char buffer[kTooSmallBufferSize + 1];

  buffer[sizeof(buffer) - 1] = kCanaryValue;

  size_t len = crashlog_to_string({buffer, sizeof(buffer) - 1}, ZirconCrashReason::Panic);
  EXPECT_LT(len, sizeof(buffer));
  EXPECT_GT(len, 0u);

  EXPECT_EQ(buffer[sizeof(buffer) - 1], kCanaryValue);

  END_TEST;
}

bool PanicBufferTest() {
  BEGIN_TEST;

  PanicBuffer b;
  ASSERT_EQ(0u, strlen(b.c_str()));
  ASSERT_EQ(0u, b.size());

  const char kMsg[] = "hello";
  const size_t kMsgLen = ::strlen(kMsg);
  b.Append(kMsg);
  ASSERT_FALSE(strcmp(b.c_str(), kMsg));
  ASSERT_EQ(kMsgLen, b.size());

  // Grossly over append.
  for (unsigned i = 0; i < PanicBuffer::kMaxSize; ++i) {
    b.Append(kMsg);
    const size_t expected_size = ktl::min((i + 2) * kMsgLen, PanicBuffer::kMaxSize - 1);
    ASSERT_EQ(expected_size, b.size());
  }
  ASSERT_EQ(PanicBuffer::kMaxSize - 1, strlen(b.c_str()));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(crashlog_tests)
UNITTEST("NoCrash", NoCrashTest)
UNITTEST("Panic/SW WDT", PanicWdtTest)
UNITTEST("OOM/RootJob", OomRootJobTest)
UNITTEST("UnknownReason", UnknownTest)
UNITTEST("Truncation", TruncationTest)
UNITTEST("PanicBuffer", PanicBufferTest)
UNITTEST_END_TESTCASE(crashlog_tests, "crashlog", "crashlog tests")
