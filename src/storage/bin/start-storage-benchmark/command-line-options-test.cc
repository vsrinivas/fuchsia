// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/bin/start-storage-benchmark/command-line-options.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace storage_benchmark {
namespace {

constexpr char kProgramName[] = "program";
constexpr char kMemfsFilesystemOption[] = "--filesystem=memfs";
constexpr char kMountPathOption[] = "--mount-path=/benchmark";

TEST(CommandLineOptionsTest, ParseCommandLineWithMinimalFlagsWorks) {
  auto result = ParseCommandLine(fxl::CommandLineFromInitializerList(
      {kProgramName, kMountPathOption, kMemfsFilesystemOption}));
  EXPECT_TRUE(result.is_ok()) << result.error_value();
}

TEST(CommandLineOptionsTest, ParseCommandLineWithoutFilesystemIsAnError) {
  auto result =
      ParseCommandLine(fxl::CommandLineFromInitializerList({kProgramName, kMountPathOption}));
  EXPECT_TRUE(result.is_error());
}

TEST(CommandLineOptionsTest, ParseCommandLineWithoutMountPathIsAnError) {
  auto result =
      ParseCommandLine(fxl::CommandLineFromInitializerList({kProgramName, kMemfsFilesystemOption}));
  EXPECT_TRUE(result.is_error());
}

TEST(CommandLineOptionsTest, ParseCommandLineWithMemfsAndZxcryptIsAnError) {
  auto result = ParseCommandLine(fxl::CommandLineFromInitializerList(
      {kProgramName, kMountPathOption, kMemfsFilesystemOption, "--zxcrypt"}));
  EXPECT_TRUE(result.is_error());
}

TEST(CommandLineOptionsTest, ParseCommandLineWithInvalidFilesystemIsAnError) {
  auto result = ParseCommandLine(fxl::CommandLineFromInitializerList(
      {kProgramName, kMountPathOption, "--filesystem=invalid"}));
  EXPECT_TRUE(result.is_error());
}

TEST(CommandLineOptionsTest, ParseCommandLineWithExtraArgumentWillPlaceThemInBenchmarkOptions) {
  std::string extraOption1 = "--extra-option1";
  std::string extraOption2 = "--extra-option2";
  auto result = ParseCommandLine(
      fxl::CommandLineFromInitializerList({kProgramName, kMemfsFilesystemOption, kMountPathOption,
                                           "--", extraOption1.c_str(), extraOption2.c_str()}));
  EXPECT_TRUE(result.is_ok()) << result.error_value();
  EXPECT_THAT(result.value().benchmark_options, testing::ElementsAre(extraOption1, extraOption2));
}

}  // namespace
}  // namespace storage_benchmark
