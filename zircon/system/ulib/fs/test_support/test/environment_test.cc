// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/test_support/environment.h"

#include <getopt.h>

#include <fbl/algorithm.h>

namespace {

using fs::Environment;

TEST(EnvironmentTest, OptionsPassThrough) {
  const char* options[] = {
      "test-name",
      "--gtest_list",
      "--gtest_filter",
      "--gtest_shuffle",
      "--gtest_repeat",
      "--gtest_random_seed",
      "--gtest_break_on_failure",
      nullptr
  };
  optind = 1;

  Environment::TestConfig config = {};
  EXPECT_TRUE(config.GetOptions(fbl::count_of(options) - 1, const_cast<char**>(options)));
  EXPECT_FALSE(config.show_help);
}

TEST(EnvironmentTest, ShortOptionsPassThrough) {
  const char* options[] = {"test-name", "-l", "-f", "-s", "-i", "-r", "-b", "-h", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  EXPECT_TRUE(config.GetOptions(fbl::count_of(options) - 1, const_cast<char**>(options)));
  EXPECT_TRUE(config.show_help);
}

TEST(EnvironmentTest, OptionalArgsPassThrough) {
  const char* options[] = {
      "test-name",
      "--gtest_list_tests=foo",
      "--gtest_filter=*.*",
      "--gtest_shuffle=false",
      "--gtest_repeat=41",
      "--gtest_random_seed=1337",
      "--gtest_break_on_failure=false",
      nullptr
  };
  optind = 1;

  Environment::TestConfig config = {};
  EXPECT_TRUE(config.GetOptions(fbl::count_of(options) - 1, const_cast<char**>(options)));
  EXPECT_FALSE(config.show_help);
}

TEST(EnvironmentTest, Help) {
  const char* options[] = {"test-name", "--help", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  EXPECT_TRUE(config.GetOptions(fbl::count_of(options) - 1, const_cast<char**>(options)));
  EXPECT_TRUE(config.show_help);
  EXPECT_NOT_NULL(config.HelpMessage());
}

TEST(EnvironmentTest, RejectsUnknownOption) {
  const char* options[] = {"test-name", "--froofy", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  EXPECT_FALSE(config.GetOptions(fbl::count_of(options) - 1, const_cast<char**>(options)));
  EXPECT_FALSE(config.show_help);
}

TEST(EnvironmentTest, ValidOptions) {
  const char* options[] = {"test-name", "--device", "path", "--no-journal", "--pager", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  EXPECT_TRUE(config.GetOptions(fbl::count_of(options) - 1, const_cast<char**>(options)));
  EXPECT_STR_EQ("path", config.physical_device_path);
  EXPECT_FALSE(config.use_journal);
}

TEST(EnvironmentTest, RejectsMissingDevice) {
  const char* options[] = {"test-name", "--device", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  EXPECT_FALSE(config.GetOptions(fbl::count_of(options) - 1, const_cast<char**>(options)));
  EXPECT_NULL(config.physical_device_path);
}

}  // namespace
