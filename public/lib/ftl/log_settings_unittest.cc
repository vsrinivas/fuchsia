// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/log_settings.h"

#include "lib/ftl/command_line.h"
#include "gtest/gtest.h"

namespace ftl {
namespace {

class LogSettingsFixture : public ::testing::Test {
 public:
  LogSettingsFixture() : old_settings_(GetLogSettings()) {}
  ~LogSettingsFixture() { SetLogSettings(old_settings_); }

 private:
  LogSettings old_settings_;
};

TEST(LogSettings, ParseValidOptions) {
  LogSettings settings;

  EXPECT_TRUE(
      ParseLogSettings(CommandLineFromInitializerList({"argv0"}), &settings));
  EXPECT_EQ(LOG_INFO, settings.min_log_level);

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
}

TEST(LogSettings, ParseInvalidOptions) {
  LogSettings settings;

  EXPECT_FALSE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--verbose=-1"}), &settings));
  EXPECT_EQ(LOG_INFO, settings.min_log_level);

  EXPECT_FALSE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--verbose=123garbage"}),
      &settings));
  EXPECT_EQ(LOG_INFO, settings.min_log_level);

  EXPECT_FALSE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--quiet=-1"}), &settings));
  EXPECT_EQ(LOG_INFO, settings.min_log_level);

  EXPECT_FALSE(ParseLogSettings(
      CommandLineFromInitializerList({"argv0", "--quiet=123garbage"}),
      &settings));
  EXPECT_EQ(LOG_INFO, settings.min_log_level);
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

}  // namespace
}  // namespace ftl
