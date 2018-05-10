// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <syslog/logger.h>

#include "gtest/gtest.h"
#include "lib/fsl/syslogger/init.h"
#include "lib/fxl/command_line.h"
#include "lib/syslog/cpp/logger.h"

__BEGIN_CDECLS

// This does not come from header file as this function should only be used in
// tests and is not for general use.
void fx_log_reset_global(void);

__END_CDECLS

namespace fsl {
namespace {

#define EXPECT_STR_EMPTY(str) EXPECT_STREQ("", str.c_str())
TEST(SysloggerSettings, ParseValidOptions) {
  syslog::LogSettings settings = {FX_LOG_ERROR, -1};

  EXPECT_STR_EMPTY(ParseLoggerSettings(
      fxl::CommandLineFromInitializerList({"argv0"}), &settings));
  EXPECT_EQ(FX_LOG_ERROR, settings.severity);

  EXPECT_STR_EMPTY(ParseLoggerSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--verbose"}), &settings));
  EXPECT_EQ(-1, settings.severity);

  EXPECT_STR_EMPTY(ParseLoggerSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--verbose=0"}),
      &settings));
  EXPECT_EQ(0, settings.severity);

  EXPECT_STR_EMPTY(ParseLoggerSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--verbose=3"}),
      &settings));
  EXPECT_EQ(-3, settings.severity);

  EXPECT_STR_EMPTY(ParseLoggerSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--quiet=0"}), &settings));
  EXPECT_EQ(0, settings.severity);

  EXPECT_STR_EMPTY(ParseLoggerSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--quiet"}), &settings));
  EXPECT_EQ(0, settings.severity);

  EXPECT_STR_EMPTY(ParseLoggerSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--quiet=3"}), &settings));
  EXPECT_EQ(3, settings.severity);

  EXPECT_STR_EMPTY(
      ParseLoggerSettings(fxl::CommandLineFromInitializerList(
                              {"argv0", "--log-file=/tmp/custom.log"}),
                          &settings));
  EXPECT_GT(settings.fd, 0);
  close(settings.fd);
}

#define EXPECT_STR_NEMPTY(str) EXPECT_STRNE(str.c_str(), "")
TEST(SysloggerSettings, ParseInvalidOptions) {
  syslog::LogSettings settings = {FX_LOG_ERROR, -1};

  EXPECT_STR_NEMPTY(ParseLoggerSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--verbose=-1"}),
      &settings));
  EXPECT_EQ(FX_LOG_ERROR, settings.severity);

  EXPECT_STR_NEMPTY(ParseLoggerSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--verbose=123garbage"}),
      &settings));
  EXPECT_EQ(FX_LOG_ERROR, settings.severity);

  EXPECT_STR_NEMPTY(ParseLoggerSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--quiet=-1"}), &settings));
  EXPECT_EQ(FX_LOG_ERROR, settings.severity);

  EXPECT_STR_NEMPTY(ParseLoggerSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--quiet=123garbage"}),
      &settings));
  EXPECT_EQ(FX_LOG_ERROR, settings.severity);

  EXPECT_STR_NEMPTY(
      ParseLoggerSettings(fxl::CommandLineFromInitializerList(
                              {"argv0", "--log-file=\\\\//invalid-path"}),
                          &settings));
  EXPECT_EQ(FX_LOG_ERROR, settings.severity);
  EXPECT_EQ(-1, settings.fd);
}

TEST(SysLoggerInit, Init) {
  fx_log_reset_global();
  ASSERT_EQ(ZX_OK,
            InitLoggerFromCommandLine(
                fxl::CommandLineFromInitializerList({"argv0", "--verbose=0"}),
                {"tag1", "tag2"}));
  fx_log_reset_global();
  ASSERT_EQ(ZX_OK,
            InitLoggerFromCommandLine(
                fxl::CommandLineFromInitializerList({"argv0", "--verbose=0"})));
  fx_log_reset_global();
}

}  // namespace
}  // namespace fsl
