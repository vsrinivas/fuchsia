// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/source_util.h"

#include <stdlib.h>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/common/scoped_temp_file.h"

namespace zxdb {

TEST(SourceUtil, GetFileContents) {
  // Make a temp file with known contents.
  ScopedTempFile temp_file;
  ASSERT_NE(-1, temp_file.fd());
  const std::string expected = "contents";
  write(temp_file.fd(), expected.data(), expected.size());

  std::string file_part(ExtractLastFileComponent(temp_file.name()));

  // Test with full input path.
  std::string contents;
  Err err = GetFileContents(temp_file.name(), "", {}, &contents);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(expected, contents);

  // With just file part, should not be found.
  err = GetFileContents(file_part, "", {}, &contents);
  EXPECT_TRUE(err.has_error());

  // With DWARF compilation dir of "/tmp" it should be found again.
  err = GetFileContents(file_part, "/tmp", {}, &contents);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(expected, contents);

  // With symbol search path it should be found.
  err = GetFileContents(file_part, "", {"/tmp"}, &contents);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(expected, contents);

  // Combination of preference and relative search path.
  err = GetFileContents(file_part, "tmp", {"/"}, &contents);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(expected, contents);
}

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
