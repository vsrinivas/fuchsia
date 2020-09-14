// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/test_support/environment.h"

#include <getopt.h>

#include <iterator>

#include <fbl/algorithm.h>

namespace {

using fs::Environment;

TEST(EnvironmentTest, OptionsPassThrough) {
  const char* options[] = {
      "test-name",      "--gtest_list",        "--gtest_filter",           "--gtest_shuffle",
      "--gtest_repeat", "--gtest_random_seed", "--gtest_break_on_failure", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  config.is_packaged = false;
  EXPECT_TRUE(config.GetOptions(std::size(options) - 1, const_cast<char**>(options)));
  EXPECT_FALSE(config.show_help);
}

TEST(EnvironmentTest, ShortOptionsPassThrough) {
  const char* options[] = {"test-name", "-l", "-f", "-s", "-i", "-r", "-b", "-h", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  config.is_packaged = false;
  EXPECT_TRUE(config.GetOptions(std::size(options) - 1, const_cast<char**>(options)));
  EXPECT_TRUE(config.show_help);
}

TEST(EnvironmentTest, OptionalArgsPassThrough) {
  const char* options[] = {"test-name",
                           "--gtest_list_tests=foo",
                           "--gtest_filter=*.*",
                           "--gtest_shuffle=false",
                           "--gtest_repeat=41",
                           "--gtest_random_seed=1337",
                           "--gtest_break_on_failure=false",
                           nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  config.is_packaged = false;
  EXPECT_TRUE(config.GetOptions(std::size(options) - 1, const_cast<char**>(options)));
  EXPECT_FALSE(config.show_help);
}

TEST(EnvironmentTest, Help) {
  const char* options[] = {"test-name", "--help", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  config.is_packaged = false;
  EXPECT_TRUE(config.GetOptions(std::size(options) - 1, const_cast<char**>(options)));
  EXPECT_TRUE(config.show_help);
  EXPECT_NOT_NULL(config.HelpMessage());
}

TEST(EnvironmentTest, RejectsUnknownOption) {
  const char* options[] = {"test-name", "--froofy", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  config.is_packaged = false;
  EXPECT_FALSE(config.GetOptions(std::size(options) - 1, const_cast<char**>(options)));
  EXPECT_FALSE(config.show_help);
}

TEST(EnvironmentTest, ValidOptions) {
  const char* options[] = {"test-name", "--device",      "path",         "--no-journal",
                           "--pager",   "--compression", "UNCOMPRESSED", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  config.is_packaged = false;
  EXPECT_TRUE(config.GetOptions(std::size(options) - 1, const_cast<char**>(options)));
  EXPECT_STR_EQ("path", config.physical_device_path);
  EXPECT_FALSE(config.use_journal);
}

TEST(EnvironmentTest, RejectsMissingDevice) {
  const char* options[] = {"test-name", "--device", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  config.is_packaged = false;
  EXPECT_FALSE(config.GetOptions(std::size(options) - 1, const_cast<char**>(options)));
  EXPECT_NULL(config.physical_device_path);
}

TEST(EnvironmentTest, ValidPowerOptions) {
  const char* options[] = {"test-name", "--power_stride", "10", "--power_start",
                           "20",        "--power_cycles", "42", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  config.is_packaged = false;
  EXPECT_TRUE(config.GetOptions(std::size(options) - 1, const_cast<char**>(options)));
  EXPECT_EQ(10, config.power_stride);
  EXPECT_EQ(20, config.power_start);
  EXPECT_EQ(42, config.power_cycles);
}

TEST(EnvironmentTest, InvalidPowerStride) {
  const char* options[] = {"test-name", "--power_stride", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  config.is_packaged = false;
  EXPECT_FALSE(config.GetOptions(std::size(options) - 1, const_cast<char**>(options)));
  EXPECT_EQ(1, config.power_stride);
}

TEST(EnvironmentTest, InvalidPowerStart) {
  const char* options[] = {"test-name", "--power_start", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  config.is_packaged = false;
  EXPECT_FALSE(config.GetOptions(std::size(options) - 1, const_cast<char**>(options)));
  EXPECT_EQ(1, config.power_start);
}

TEST(EnvironmentTest, InvalidPowerCycles) {
  const char* options[] = {"test-name", "--power_cycles", nullptr};
  optind = 1;

  Environment::TestConfig config = {};
  config.is_packaged = false;
  EXPECT_FALSE(config.GetOptions(std::size(options) - 1, const_cast<char**>(options)));
  EXPECT_EQ(5, config.power_cycles);
}

}  // namespace
