// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/log_settings.h"

#include <unistd.h>

#include "gtest/gtest.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"

namespace fxl {
namespace {

class LogSettingsFixture : public ::testing::Test {
 public:
  LogSettingsFixture()
      : old_settings_(GetLogSettings()), old_stderr_(dup(STDERR_FILENO)) {}
  ~LogSettingsFixture() {
    SetLogSettings(old_settings_);
    dup2(old_stderr_.get(), STDERR_FILENO);
  }

 private:
  LogSettings old_settings_;
  fxl::UniqueFD old_stderr_;
};

TEST(LogSettings, DefaultOptions) {
  LogSettings settings;
  EXPECT_EQ(LOG_INFO, settings.min_log_level);
  EXPECT_EQ(std::string(), settings.log_file);
}

TEST(LogSettings, ParseValidOptions) {
  LogSettings settings;
  settings.min_log_level = LOG_FATAL;

  EXPECT_TRUE(
      ParseLogSettings(CommandLineFromInitializerList({"argv0"}), &settings));
  EXPECT_EQ(LOG_FATAL, settings.min_log_level);

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--verbose"}), &settings));
  EXPECT_EQ(-1, settings.min_log_level);

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--verbose=0"}), &settings));
  EXPECT_EQ(0, settings.min_log_level);

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--verbose=3"}), &settings));
  EXPECT_EQ(-3, settings.min_log_level);

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--quiet=0"}), &settings));
  EXPECT_EQ(0, settings.min_log_level);

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--quiet"}), &settings));
  EXPECT_EQ(1, settings.min_log_level);

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--quiet=3"}), &settings));
  EXPECT_EQ(3, settings.min_log_level);

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--log-file=/tmp/custom.log"}),
      &settings));
  EXPECT_EQ("/tmp/custom.log", settings.log_file);
}

TEST(LogSettings, ParseInvalidOptions) {
  LogSettings settings;
  settings.min_log_level = LOG_FATAL;

  EXPECT_FALSE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--verbose=-1"}), &settings));
  EXPECT_EQ(LOG_FATAL, settings.min_log_level);

  EXPECT_FALSE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--verbose=123garbage"}),
      &settings));
  EXPECT_EQ(LOG_FATAL, settings.min_log_level);

  EXPECT_FALSE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--quiet=-1"}), &settings));
  EXPECT_EQ(LOG_FATAL, settings.min_log_level);

  EXPECT_FALSE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--quiet=123garbage"}),
      &settings));
  EXPECT_EQ(LOG_FATAL, settings.min_log_level);
}

TEST_F(LogSettingsFixture, SetAndGet) {
  LogSettings new_settings;
  new_settings.min_log_level = -20;
  SetLogSettings(new_settings);

  LogSettings current_settings = GetLogSettings();
  EXPECT_EQ(new_settings.min_log_level, current_settings.min_log_level);
  EXPECT_EQ(new_settings.min_log_level, GetMinLogLevel());
}

TEST_F(LogSettingsFixture, SetValidOptions) {
  EXPECT_TRUE(SetLogSettingsFromCommandLine(
      CommandLineFromInitializerList({"argv0", "--verbose=20"})));

  LogSettings current_settings = GetLogSettings();
  EXPECT_EQ(-20, current_settings.min_log_level);
  EXPECT_EQ(-20, GetMinLogLevel());
}

TEST_F(LogSettingsFixture, SetInvalidOptions) {
  LogSettings old_settings = GetLogSettings();

  EXPECT_FALSE(SetLogSettingsFromCommandLine(
      CommandLineFromInitializerList({"argv0", "--verbose=garbage"})));

  LogSettings current_settings = GetLogSettings();
  EXPECT_EQ(old_settings.min_log_level, current_settings.min_log_level);
  EXPECT_EQ(old_settings.min_log_level, GetMinLogLevel());
}

TEST_F(LogSettingsFixture, SetValidLogFile) {
  const char kTestMessage[] = "TEST MESSAGE";

  LogSettings new_settings;
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.NewTempFile(&new_settings.log_file));
  SetLogSettings(new_settings);

  LogSettings current_settings = GetLogSettings();
  EXPECT_EQ(new_settings.log_file, current_settings.log_file);
  FXL_LOG(INFO) << kTestMessage;

  ASSERT_EQ(0, access(new_settings.log_file.c_str(), R_OK));
  std::string log;
  ASSERT_TRUE(files::ReadFileToString(new_settings.log_file, &log));
  EXPECT_TRUE(log.find(kTestMessage) != std::string::npos);
}

TEST_F(LogSettingsFixture, SetInvalidLogFile) {
  LogSettings old_settings = GetLogSettings();

  LogSettings new_settings;
  new_settings.log_file = "\\\\//invalid-path";
  SetLogSettings(new_settings);

  LogSettings current_settings = GetLogSettings();
  EXPECT_EQ(old_settings.log_file.c_str(), current_settings.log_file);

  EXPECT_NE(0, access(new_settings.log_file.c_str(), R_OK));
}

TEST_F(LogSettingsFixture, ToArgv) {
  LogSettings settings;
  EXPECT_TRUE(LogSettingsToArgv(settings).empty());

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--quiet"}), &settings));
  EXPECT_TRUE(LogSettingsToArgv(settings) ==
              std::vector<std::string>{"--quiet=1"});

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--quiet=3"}), &settings));
  EXPECT_TRUE(LogSettingsToArgv(settings) ==
              std::vector<std::string>{"--quiet=3"});

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--verbose"}), &settings));
  EXPECT_TRUE(LogSettingsToArgv(settings) ==
              std::vector<std::string>{"--verbose=1"});

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--verbose=3"}), &settings));
  EXPECT_TRUE(LogSettingsToArgv(settings) ==
              std::vector<std::string>{"--verbose=3"});

  // Reset |settings| back to defaults so we don't pick up previous tests.
  settings = LogSettings{};
  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--log-file=/foo"}), &settings));
  EXPECT_TRUE(LogSettingsToArgv(settings) ==
              std::vector<std::string>{"--log-file=/foo"}) << LogSettingsToArgv(settings)[0];

  EXPECT_TRUE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--verbose", "--log-file=/foo"}),
      &settings));
  EXPECT_TRUE(LogSettingsToArgv(settings) ==
              (std::vector<std::string>{"--verbose=1", "--log-file=/foo"}));
}

}  // namespace
}  // namespace fxl
