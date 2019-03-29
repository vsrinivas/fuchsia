// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/source_util.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(SourceUtil, ExtractSourceLines) {
  std::string contents =
      "one\n"
      "two\r"
      "three\r\n"
      "four";  // No end-of-file newline.

  // Variant that returns all lines.
  std::vector<std::string> results = ExtractSourceLines(contents);
  std::vector<std::string> expected{"one", "two", "three", "four"};
  EXPECT_EQ(expected, results);

  // Line range.
  results = ExtractSourceLines(contents, 2, 3);
  expected = {"two", "three"};
  EXPECT_EQ(expected, results);

  // Off end.
  results = ExtractSourceLines(contents, 100, 101);
  EXPECT_TRUE(results.empty());

  // End-of-file newline.
  contents.push_back('\n');
  results = ExtractSourceLines(contents);
  expected = {"one", "two", "three", "four"};
  EXPECT_EQ(expected, results);

  // Make a blank line at the end.
  contents.push_back(' ');
  results = ExtractSourceLines(contents);
  expected.push_back(" ");
  EXPECT_EQ(expected, results);
}

}  // namespace zxdb
