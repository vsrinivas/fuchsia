// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/test/test_settings.h"

#include <stdlib.h>
#include <unistd.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings.h"

namespace fxl {
namespace {
using ::testing::StrEq;

// Saves and restores global state impacted by |SetTestSettings| before and
// after each test, so that this test suite may be run without impacting
// customized test parameters set by the user (for other tests in the same run).
class TestSettingsFixture : public ::testing::Test {
 public:
  TestSettingsFixture()
      : old_settings_(GetLogSettings()),
        old_stderr_(dup(STDERR_FILENO)),
        random_seed_(getenv("TEST_LOOP_RANDOM_SEED")) {}
  ~TestSettingsFixture() {
    SetLogSettings(old_settings_);
    dup2(old_stderr_.get(), STDERR_FILENO);
    if (random_seed_) {
      setenv("TEST_LOOP_RANDOM_SEED", random_seed_, /*overwrite=*/true);
    } else {
      unsetenv("TEST_LOOP_RANDOM_SEED");
    }
  }

 private:
  LogSettings old_settings_;
  fxl::UniqueFD old_stderr_;
  char *random_seed_;
};

// Test that --test_loop_seed sets TEST_LOOP_RANDOM_SEED.
// Because FXL is cross-platform, we cannot test that the environment variable
// correctly propagates the random seed to the test loop, which is
// Fuchsia-specific. This propagation test is performed in
// //garnet/public/lib/gtest/test_loop_fixture_unittest.cc instead.
TEST_F(TestSettingsFixture, RandomSeed) {
  EXPECT_TRUE(SetTestSettings(
      CommandLineFromInitializerList({"argv0", "--test_loop_seed=1"})));
  EXPECT_THAT(getenv("TEST_LOOP_RANDOM_SEED"), StrEq("1"));
  const char *argv[] = {"argv0", "--test_loop_seed=2", nullptr};
  EXPECT_TRUE(SetTestSettings(2, argv));
  EXPECT_THAT(getenv("TEST_LOOP_RANDOM_SEED"), StrEq("2"));
}

TEST_F(TestSettingsFixture, LogLevel) {
  EXPECT_TRUE(SetTestSettings(
      CommandLineFromInitializerList({"argv0", "--verbose=21"})));
  EXPECT_EQ(GetMinLogLevel(), -21);
  // The value for --quiet needs to be smaller than LOG_FATAL because
  // min_log_level is capped at LOG_FATAL.
  const char *argv[] = {"argv0", "--quiet=2", nullptr};
  EXPECT_TRUE(SetTestSettings(2, argv));
  EXPECT_EQ(GetMinLogLevel(), 2);
}

}  // namespace
}  // namespace fxl
