// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/logging.h"

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/log_settings.h"

#ifdef __Fuchsia__
#include <lib/syslog/global.h>
#include <lib/syslog/logger.h>
#include <lib/syslog/wire_format.h>
#include <lib/zx/socket.h>
#endif

namespace fxl {
namespace {

class LoggingFixture : public ::testing::Test {
 public:
  LoggingFixture() : old_severity_(GetMinLogLevel()), old_stderr_(dup(STDERR_FILENO)) {}
  ~LoggingFixture() {
    SetLogSettings({.min_log_level = old_severity_});
#ifdef __Fuchsia__
    // Go back to using STDERR.
    fx_logger_t* logger = fx_log_get_logger();
    fx_logger_activate_fallback(logger, -1);
#else
    dup2(old_stderr_, STDERR_FILENO);
#endif
  }

 private:
  LogSeverity old_severity_;
  int old_stderr_;
};

using LoggingFixtureDeathTest = LoggingFixture;

TEST_F(LoggingFixture, Log) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  int error_line = __LINE__ + 1;
  FXL_LOG(ERROR) << "something at error";

  int info_line = __LINE__ + 1;
  FXL_LOG(INFO) << "and some other at info level";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

#ifdef __Fuchsia__
  EXPECT_THAT(log, testing::HasSubstr("ERROR: [src/lib/fxl/logging_unittest.cc(" +
                                      std::to_string(error_line) + ")] something at error"));

  EXPECT_THAT(log, testing::HasSubstr("INFO: [logging_unittest.cc(" + std::to_string(info_line) +
                                      ")] and some other at info level"));
#else
  EXPECT_THAT(log, testing::HasSubstr("[ERROR:src/lib/fxl/logging_unittest.cc(" +
                                      std::to_string(error_line) + ")] something at error"));

  EXPECT_THAT(log, testing::HasSubstr("[INFO:logging_unittest.cc(" + std::to_string(info_line) +
                                      ")] and some other at info level"));

#endif
}

TEST_F(LoggingFixture, LogFirstN) {
  constexpr int kLimit = 5;
  constexpr int kCycles = 20;
  constexpr const char* kLogMessage = "Hello";
  static_assert(kCycles > kLimit);

  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  for (int i = 0; i < kCycles; ++i) {
    FXL_LOG_FIRST_N(ERROR, kLimit) << kLogMessage;
  }

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

  int count = 0;
  size_t pos = 0;
  while ((pos = log.find(kLogMessage, pos)) != std::string::npos) {
    ++count;
    ++pos;
  }
  EXPECT_EQ(kLimit, count);
}

TEST_F(LoggingFixture, LogT) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  int error_line = __LINE__ + 1;
  FXL_LOGT(ERROR, "first") << "something at error";

  int info_line = __LINE__ + 1;
  FXL_LOGT(INFO, "second") << "and some other at info level";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

#ifdef __Fuchsia__
  EXPECT_THAT(log, testing::HasSubstr("first] ERROR: [src/lib/fxl/logging_unittest.cc(" +
                                      std::to_string(error_line) + ")] something at error"));

  EXPECT_THAT(log,
              testing::HasSubstr("second] INFO: [logging_unittest.cc(" + std::to_string(info_line) +
                                 ")] and some other at info level"));
#else
  EXPECT_THAT(log, testing::HasSubstr("[first] [ERROR:src/lib/fxl/logging_unittest.cc(" +
                                      std::to_string(error_line) + ")] something at error"));

  EXPECT_THAT(log,
              testing::HasSubstr("[second] [INFO:logging_unittest.cc(" + std::to_string(info_line) +
                                 ")] and some other at info level"));

#endif
}

TEST_F(LoggingFixture, VLogT) {
  LogSettings new_settings;
  new_settings.min_log_level = -1;
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings, {});

  int line = __LINE__ + 1;
  FXL_VLOGT(1, "first") << "First message";
  FXL_VLOGT(2, "second") << "ABCD";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

#ifdef __Fuchsia__
  EXPECT_THAT(log, testing::HasSubstr("[first] VLOG(1): [logging_unittest.cc(" +
                                      std::to_string(line) + ")] First message"));
#else
  EXPECT_THAT(log, testing::HasSubstr("[first] [VERBOSE1:logging_unittest.cc(" +
                                      std::to_string(line) + ")] First message"));
#endif

  EXPECT_THAT(log, testing::Not(testing::HasSubstr("second")));
  EXPECT_THAT(log, testing::Not(testing::HasSubstr("ABCD")));
}

TEST_F(LoggingFixture, DVLogNoMinLevel) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  FXL_DVLOG(1) << "hello";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

  EXPECT_EQ(log, "");
}

TEST_F(LoggingFixture, DVLogWithMinLevel) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  new_settings.min_log_level = -1;
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  FXL_DVLOG(1) << "hello";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

#if defined(NDEBUG)
  EXPECT_EQ(log, "");
#else
  EXPECT_THAT(log, testing::HasSubstr("hello"));
#endif
}

TEST_F(LoggingFixtureDeathTest, CheckFailed) { ASSERT_DEATH(FXL_CHECK(false), ""); }

#if defined(__Fuchsia__)
TEST_F(LoggingFixture, Plog) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  FXL_PLOG(ERROR, ZX_OK) << "should be ok";
  FXL_PLOG(ERROR, ZX_ERR_ACCESS_DENIED) << "got access denied";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

  EXPECT_THAT(log, testing::HasSubstr("should be ok: 0 (ZX_OK)"));
  EXPECT_THAT(log, testing::HasSubstr("got access denied: -30 (ZX_ERR_ACCESS_DENIED)"));
}

TEST_F(LoggingFixture, PlogT) {
  LogSettings new_settings;
  EXPECT_EQ(LOG_INFO, new_settings.min_log_level);
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  int line1 = __LINE__ + 1;
  FXL_PLOGT(ERROR, "abcd", ZX_OK) << "should be ok";

  int line2 = __LINE__ + 1;
  FXL_PLOGT(ERROR, "qwerty", ZX_ERR_ACCESS_DENIED) << "got access denied";

  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));

  EXPECT_THAT(log, testing::HasSubstr("abcd] ERROR: [src/lib/fxl/logging_unittest.cc(" +
                                      std::to_string(line1) + ")] should be ok: 0 (ZX_OK)"));
  EXPECT_THAT(log, testing::HasSubstr("qwerty] ERROR: [src/lib/fxl/logging_unittest.cc(" +
                                      std::to_string(line2) +
                                      ")] got access denied: -30 (ZX_ERR_ACCESS_DENIED)"));
}
#endif  // defined(__Fuchsia__)

}  // namespace
}  // namespace fxl
