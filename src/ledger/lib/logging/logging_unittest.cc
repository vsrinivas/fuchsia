// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/logging/logging.h"

#include <iostream>
#include <sstream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "third_party/abseil-cpp/absl/base/log_severity.h"

namespace ledger {
namespace {

using ::testing::IsEmpty;
using ::testing::MatchesRegex;

class LoggingTest : public ::testing::Test {
 public:
  LoggingTest() {
    // Make std::cerr write to a stringstream.
    // Intercepting std::cerr instead of stderr allows gtest messages to be printed.
    cerr_to_restore_ = std::cerr.rdbuf();
    std::cerr.rdbuf(output_.rdbuf());
    // Restore the default logging severity.
    SetLogSeverity(absl::LogSeverity::kInfo);
  }

  ~LoggingTest() {
    // Restore std::cerr.
    std::cerr.rdbuf(cerr_to_restore_);
  }

  // Checks that std::cerr is flushed and returns all the error output seen until now.
  std::string ReadCerr() {
    std::string output = output_.str();
    output_.str("");
    std::cerr.flush();
    EXPECT_THAT(output_.str(), IsEmpty());
    return output;
  }

 private:
  std::stringstream output_;
  std::streambuf* cerr_to_restore_;
};

using LoggingDeathTest = ::testing::Test;

TEST_F(LoggingTest, LogSeverity) {
  SetLogSeverity(absl::LogSeverity::kFatal);
  EXPECT_EQ(GetLogSeverity(), absl::LogSeverity::kFatal);

  SetLogVerbosity(2);
  EXPECT_EQ((int)GetLogSeverity(), -2);

  SetLogSeverity((absl::LogSeverity)-1);
  EXPECT_EQ((int)GetLogSeverity(), -1);
}

TEST_F(LoggingTest, LogInfo) {
  LEDGER_LOG(INFO) << "Log message";
  EXPECT_THAT(ReadCerr(), MatchesRegex("\\[INFO:.*logging_unittest.cc.*\\] Log message\n"));

  SetLogSeverity(absl::LogSeverity::kWarning);
  LEDGER_LOG(INFO) << "Not displayed";
  EXPECT_THAT(ReadCerr(), IsEmpty());
}

TEST_F(LoggingTest, LogWarning) {
  LEDGER_LOG(WARNING) << "Log message";
  EXPECT_THAT(ReadCerr(), MatchesRegex("\\[WARNING:.*logging_unittest.cc.*\\] Log message\n"));

  SetLogSeverity(absl::LogSeverity::kWarning);
  LEDGER_LOG(WARNING) << "Other log message";
  EXPECT_THAT(ReadCerr(),
              MatchesRegex("\\[WARNING:.*logging_unittest.cc.*\\] Other log message\n"));

  SetLogSeverity(absl::LogSeverity::kError);
  LEDGER_LOG(WARNING) << "Not displayed";
  EXPECT_THAT(ReadCerr(), IsEmpty());
}

TEST_F(LoggingTest, LogError) {
  LEDGER_LOG(ERROR) << "Log message";
  EXPECT_THAT(ReadCerr(), MatchesRegex("\\[ERROR:.*logging_unittest.cc.*\\] Log message\n"));

  SetLogSeverity(absl::LogSeverity::kWarning);
  LEDGER_LOG(ERROR) << "Other log message";
  EXPECT_THAT(ReadCerr(), MatchesRegex("\\[ERROR:.*logging_unittest.cc.*\\] Other log message\n"));

  SetLogSeverity(absl::LogSeverity::kFatal);
  LEDGER_LOG(ERROR) << "Not displayed";
  EXPECT_THAT(ReadCerr(), IsEmpty());
}

TEST_F(LoggingDeathTest, LogFatal) {
  ASSERT_DEATH({ LEDGER_LOG(FATAL) << "bad things happened"; },
               "\\[FATAL:.*logging_unittest.cc.*\\] bad things happened\n");
}

TEST_F(LoggingDeathTest, LogFatalAlwaysPrinted) {
  SetLogSeverity((absl::LogSeverity)((int)absl::LogSeverity::kFatal + 1));
  ASSERT_DEATH({ LEDGER_LOG(FATAL) << "still printed"; },
               "\\[FATAL:.*logging_unittest.cc.*\\] still printed\n");
}

TEST_F(LoggingTest, LogVerbose1) {
  LEDGER_VLOG(1) << "Not displayed";
  EXPECT_THAT(ReadCerr(), IsEmpty());

  SetLogVerbosity(1);
  LEDGER_VLOG(1) << "Log message";
  EXPECT_THAT(ReadCerr(), MatchesRegex("\\[VERBOSE1:.*logging_unittest.cc.*\\] Log message\n"));
}

TEST_F(LoggingTest, LogVerbose2) {
  SetLogVerbosity(1);
  LEDGER_VLOG(2) << "Not displayed";
  EXPECT_THAT(ReadCerr(), IsEmpty());

  SetLogVerbosity(2);
  LEDGER_VLOG(2) << "Log message";
  EXPECT_THAT(ReadCerr(), MatchesRegex("\\[VERBOSE2:.*logging_unittest.cc.*\\] Log message\n"));
}

TEST_F(LoggingTest, CheckSuccess) { LEDGER_CHECK(true); }

TEST_F(LoggingDeathTest, CheckFailure) {
  bool condition = false;
  ASSERT_DEATH({ LEDGER_CHECK(condition) << "oh no"; },
               "\\[FATAL:.*logging_unittest.cc.*\\] Check failed: condition. oh no\n");
}

TEST_F(LoggingTest, DCheckSuccess) { LEDGER_DCHECK(true); }

TEST_F(LoggingTest, DCheckIgnoredInRelease) {
  if (LEDGER_DEBUG) {
    GTEST_SKIP();
  }
  LEDGER_DCHECK(false);
}

TEST_F(LoggingDeathTest, DCheckFailure) {
  if (!LEDGER_DEBUG) {
    GTEST_SKIP();
  }
  bool condition = false;
  ASSERT_DEATH({ LEDGER_DCHECK(condition) << "oh no"; },
               "\\[FATAL:.*logging_unittest.cc.*\\] Check failed: condition. oh no\n");
}

TEST_F(LoggingTest, NotReachedIgnoredInRelease) {
  if (LEDGER_DEBUG) {
    GTEST_SKIP();
  }
  LEDGER_NOTREACHED();
}

TEST_F(LoggingDeathTest, NotReached) {
  if (!LEDGER_DEBUG) {
    GTEST_SKIP();
  }
  ASSERT_DEATH({ LEDGER_NOTREACHED(); },
               "\\[FATAL:.*logging_unittest.cc.*\\] Check failed: false. Unreachable. \n");
}

TEST_F(LoggingTest, NotImplemented) {
  LEDGER_NOTIMPLEMENTED();
  EXPECT_THAT(ReadCerr(), MatchesRegex("\\[ERROR:.*logging_unittest.cc.*\\] Not implemented. \n"));
}

}  // namespace
}  // namespace ledger
