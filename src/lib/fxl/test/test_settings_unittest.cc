// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/test/test_settings.h"

#include <lib/syslog/cpp/log_settings.h>
#include <stdlib.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "sdk/lib/syslog/cpp/log_level.h"
#include "src/lib/fxl/command_line.h"

namespace fxl {
namespace {
using ::testing::StrEq;

// Saves and restores global state impacted by |SetTestSettings| before and
// after each test, so that this test suite may be run without impacting
// customized test parameters set by the user (for other tests in the same run).
class TestSettingsFixture : public ::testing::Test {
 public:
  TestSettingsFixture()
      : old_severity_(syslog::GetMinLogLevel()),
        old_stderr_(dup(STDERR_FILENO)),
        random_seed_(getenv("TEST_LOOP_RANDOM_SEED")) {}
  ~TestSettingsFixture() {
    syslog::SetLogSettings({.min_log_level = old_severity_});
    dup2(old_stderr_.get(), STDERR_FILENO);
    if (random_seed_) {
      setenv("TEST_LOOP_RANDOM_SEED", random_seed_, /*overwrite=*/true);
    } else {
      unsetenv("TEST_LOOP_RANDOM_SEED");
    }
  }

 private:
  syslog::LogSeverity old_severity_;
  fbl::unique_fd old_stderr_;
  char *random_seed_;
};

// Test that --test_loop_seed sets TEST_LOOP_RANDOM_SEED.
// Because FXL is cross-platform, we cannot test that the environment variable
// correctly propagates the random seed to the test loop, which is
// Fuchsia-specific. This propagation test is performed in
// //src/lib/testing/loop_fixture/test_loop_fixture_unittest.cc instead.
TEST_F(TestSettingsFixture, RandomSeed) {
  EXPECT_TRUE(SetTestSettings(CommandLineFromInitializerList({"argv0", "--test_loop_seed=1"})));
  EXPECT_THAT(getenv("TEST_LOOP_RANDOM_SEED"), StrEq("1"));
  const char *argv[] = {"argv0", "--test_loop_seed=2", nullptr};
  EXPECT_TRUE(SetTestSettings(2, argv));
  EXPECT_THAT(getenv("TEST_LOOP_RANDOM_SEED"), StrEq("2"));
}

TEST_F(TestSettingsFixture, LogLevel) {
  EXPECT_TRUE(SetTestSettings(CommandLineFromInitializerList({"argv0", "--verbose=10"})));
  EXPECT_EQ(syslog::GetMinLogLevel(), 0x26);  // INFO(0x30) - 10
  // The value for --quiet needs to be smaller than LOG_FATAL because
  // min_log_level is capped at LOG_FATAL.
  const char *argv[] = {"argv0", "--quiet=2", nullptr};
  EXPECT_TRUE(SetTestSettings(2, argv));
  EXPECT_EQ(syslog::GetMinLogLevel(), syslog::LOG_ERROR);
}

}  // namespace
}  // namespace fxl
