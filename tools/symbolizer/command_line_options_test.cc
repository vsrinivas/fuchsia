// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/command_line_options.h"

#include <gtest/gtest.h>

namespace symbolizer {

namespace {

TEST(CommandLineOptionsTest, ValidOptions) {
  CommandLineOptions options;
  const char* argv[] = {"", "--ids-txt=path/to/ids.txt", "-s", "/symbol/path"};

  const Error error = ParseCommandLine(sizeof(argv) / sizeof(char*), argv, &options);
  ASSERT_TRUE(error.empty());
  ASSERT_TRUE(options.symbol_index_files.empty());
  ASSERT_EQ(options.ids_txts.size(), 1UL);
  ASSERT_EQ(options.ids_txts[0], "path/to/ids.txt");
  ASSERT_EQ(options.symbol_paths.size(), 1UL);
  ASSERT_EQ(options.symbol_paths[0], "/symbol/path");
}

TEST(CommandLineOptionsTest, InvalidOptions) {
  CommandLineOptions options;
  const char* argv[] = {"", "--invalid"};

  const Error error = ParseCommandLine(sizeof(argv) / sizeof(char*), argv, &options);
  ASSERT_FALSE(error.empty());
}

}  // namespace

}  // namespace symbolizer
