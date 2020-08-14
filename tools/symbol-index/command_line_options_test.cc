// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbol-index/command_line_options.h"

#include <gtest/gtest.h>

namespace symbol_index {

namespace {

TEST(CommandLineOptionsTest, List) {
  CommandLineOptions options;
  const char* argv[] = {"", "list"};

  const Error error = ParseCommandLine(sizeof(argv) / sizeof(char*), argv, &options);
  ASSERT_TRUE(error.empty());
  ASSERT_TRUE(options.symbol_index_file.empty());
  ASSERT_EQ(options.verb, CommandLineOptions::Verb::kList);
  ASSERT_TRUE(options.params.empty());
}

TEST(CommandLineOptionsTest, Add) {
  CommandLineOptions options;
  const char* argv[] = {"", "add", "/some/symbol_path"};

  const Error error = ParseCommandLine(sizeof(argv) / sizeof(char*), argv, &options);
  ASSERT_EQ(options.verb, CommandLineOptions::Verb::kAdd);
  ASSERT_EQ(options.params.size(), 1UL);
  ASSERT_EQ(options.params[0], "/some/symbol_path");
}

TEST(CommandLineOptionsTest, AddTwoArgs) {
  CommandLineOptions options;
  const char* argv[] = {"", "add", "/some/symbol_path", "/some/build_dir"};

  const Error error = ParseCommandLine(sizeof(argv) / sizeof(char*), argv, &options);
  ASSERT_EQ(options.verb, CommandLineOptions::Verb::kAdd);
  ASSERT_EQ(options.params.size(), 2UL);
  ASSERT_EQ(options.params[1], "/some/build_dir");
}

TEST(CommandLineOptionsTest, InvalidVerb) {
  CommandLineOptions options;
  const char* argv[] = {"", "addd", "/some/symbol_path", "/some/build_dir"};

  const Error error = ParseCommandLine(sizeof(argv) / sizeof(char*), argv, &options);
  ASSERT_FALSE(error.empty());
}

TEST(CommandLineOptionsTest, InvalidNumOfArgs) {
  CommandLineOptions options;
  const char* argv[] = {"", "list", "/some/symbol_path"};

  const Error error = ParseCommandLine(sizeof(argv) / sizeof(char*), argv, &options);
  ASSERT_FALSE(error.empty());
}

TEST(CommandLineOptionsTest, CustomPath) {
  CommandLineOptions options;
  const char* argv[] = {"", "-c", "path/to/config", "list"};

  const Error error = ParseCommandLine(sizeof(argv) / sizeof(char*), argv, &options);
  ASSERT_EQ(options.symbol_index_file, "path/to/config");
}

}  // namespace

}  // namespace symbol_index
