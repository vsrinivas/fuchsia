// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/bin/start-storage-benchmark/command-line-options.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace storage_benchmark {
namespace {

constexpr char kProgramName[] = "program";
constexpr char kBenchmarkUrlOption[] =
    "--benchmark-url=fuchsia-pkg://fuchsia.com/package#meta/component.cmx";
constexpr char kMemfsFilesystemOption[] = "--filesystem=memfs";
constexpr char kMountPathOption[] = "--mount-path=/benchmark";

TEST(CommandLineOptionsTest, ParseCommandLineWithMinimalFlagsWorks) {
  const char* argv[] = {
      kProgramName, kBenchmarkUrlOption, kMountPathOption, kMemfsFilesystemOption, nullptr,
  };
  auto result = ParseCommandLine(std::size(argv) - 1, argv);
  EXPECT_TRUE(result.is_ok()) << result.error_value();
}

TEST(CommandLineOptionsTest, ParseCommandLineWithoutFilesystemIsAnError) {
  const char* argv[] = {kProgramName, kBenchmarkUrlOption, kMountPathOption, nullptr};
  auto result = ParseCommandLine(std::size(argv) - 1, argv);
  EXPECT_TRUE(result.is_error());
}

TEST(CommandLineOptionsTest, ParseCommandLineWithoutBenchmarkUrlIsAnError) {
  const char* argv[] = {kProgramName, kMountPathOption, kMemfsFilesystemOption, nullptr};
  auto result = ParseCommandLine(std::size(argv) - 1, argv);
  EXPECT_TRUE(result.is_error());
}

TEST(CommandLineOptionsTest, ParseCommandLineWithoutMountPathIsAnError) {
  const char* argv[] = {kProgramName, kBenchmarkUrlOption, kMemfsFilesystemOption, nullptr};
  auto result = ParseCommandLine(std::size(argv) - 1, argv);
  EXPECT_TRUE(result.is_error());
}

TEST(CommandLineOptionsTest, ParseCommandLineWithMemfsAndZxcryptIsAnError) {
  const char* argv[] = {kProgramName,           kBenchmarkUrlOption, kMountPathOption,
                        kMemfsFilesystemOption, "--zxcrypt",         nullptr};
  auto result = ParseCommandLine(std::size(argv) - 1, argv);
  EXPECT_TRUE(result.is_error());
}

TEST(CommandLineOptionsTest, ParseCommandLineWithInvalidFilesystemIsAnError) {
  const char* argv[] = {kProgramName, kBenchmarkUrlOption, kMountPathOption, "--filesystem=invalid",
                        nullptr};
  auto result = ParseCommandLine(std::size(argv) - 1, argv);
  EXPECT_TRUE(result.is_error());
}

TEST(CommandLineOptionsTest, ParseCommandLineWithExtraArgumentWillPlaceThemInBenchmarkOptions) {
  std::string extraOption1 = "--extra-option1";
  std::string extraOption2 = "--extra-option2";
  const char* argv[] = {
      kProgramName, kMemfsFilesystemOption, kBenchmarkUrlOption,  kMountPathOption,
      "--",         extraOption1.c_str(),   extraOption2.c_str(), nullptr};
  auto result = ParseCommandLine(std::size(argv) - 1, argv);
  EXPECT_TRUE(result.is_ok()) << result.error_value();
  EXPECT_THAT(result.value().benchmark_options, testing::ElementsAre(extraOption1, extraOption2));
}

}  // namespace
}  // namespace storage_benchmark
