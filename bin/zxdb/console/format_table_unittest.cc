// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_table.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(FormatTable, Basic) {
  // Test with no data.
  OutputBuffer out;
  std::vector<std::vector<std::string>> rows;
  FormatTable({ColSpec(), ColSpec()}, rows, &out);
  EXPECT_EQ("", out.AsString());

  // Heading only.
  out = OutputBuffer();
  FormatTable(
      {ColSpec(Align::kLeft, 0, "One"), ColSpec(Align::kLeft, 0, "Two")}, rows,
      &out);
  EXPECT_EQ("One Two\n", out.AsString());

  // Two rows for all tests below.
  rows.push_back(std::vector<std::string>{"0", "Hello, world"});
  rows.push_back(std::vector<std::string>{"12345", "Hello"});

  // Left align.
  out = OutputBuffer();
  FormatTable({ColSpec(), ColSpec()}, rows, &out);
  EXPECT_EQ("0     Hello, world\n12345 Hello\n", out.AsString());

  // Right align with padding.
  out = OutputBuffer();
  FormatTable(
      {ColSpec(Align::kRight), ColSpec(Align::kRight, 0, std::string(), 2)},
      rows, &out);
  EXPECT_EQ("    0   Hello, world\n12345          Hello\n", out.AsString());

  // Max width + heading.
  out = OutputBuffer();
  FormatTable(
      {ColSpec(Align::kRight, 3, "One"), ColSpec(Align::kLeft, 3, "Two")}, rows,
      &out);
  EXPECT_EQ("One Two\n  0 Hello, world\n12345 Hello\n", out.AsString());

  // Overflowing cells shouldn't force the whole column to max width.
  out = OutputBuffer();
  FormatTable({ColSpec(Align::kRight, 1), ColSpec(Align::kLeft, 0)}, rows,
              &out);
  EXPECT_EQ("0 Hello, world\n12345 Hello\n", out.AsString());

  // The last item in a row expands to fill all remaining columns. Make the
  // last row have only one really long string.
  out = OutputBuffer();
  rows[1].resize(1);
  rows[1][0] = "This is a really long contents.";
  FormatTable({ColSpec(Align::kLeft, 1), ColSpec(Align::kLeft, 0)}, rows, &out);
  EXPECT_EQ("0 Hello, world\nThis is a really long contents.\n", out.AsString());
}

}  // namespace zxdb
