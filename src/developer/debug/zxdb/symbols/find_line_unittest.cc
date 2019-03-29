// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/find_line.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/mock_line_table.h"

namespace zxdb {

namespace {

llvm::DWARFDebugLine::Row MakeStatementRow(uint64_t address, uint16_t file,
                                           uint32_t line) {
  llvm::DWARFDebugLine::Row result;
  result.Address = address;
  result.Line = line;
  result.Column = 0;
  result.File = file;
  result.Discriminator = 0;
  result.Isa = 0;
  result.IsStmt = 1;
  result.BasicBlock = 0;
  result.EndSequence = 0;
  result.PrologueEnd = 0;
  result.EpilogueBegin = 0;

  return result;
}

}  // namespace

TEST(FindLine, GetAllLineTableMatchesInUnit) {
  MockLineTable::FileNameVector files;
  files.push_back("file1.cc");  // Name for file ID #1.
  files.push_back("file2.cc");  // Name for file ID #2.

  MockLineTable::RowVector rows;
  rows.push_back(MakeStatementRow(0x1000, 1, 1));  // File #1, line 1.
  rows.push_back(MakeStatementRow(0x1001, 1, 2));
  rows.push_back(MakeStatementRow(0x1002, 2, 1));  // File #2, line 1.
  rows.push_back(MakeStatementRow(0x1003, 1, 1));  // Dupe for File 1, line 1.
  rows.push_back(MakeStatementRow(0x1004, 1, 90));
  rows.push_back(MakeStatementRow(0x1005, 1, 100));
  rows.push_back(MakeStatementRow(0x1006, 1, 95));
  rows.push_back(MakeStatementRow(0x1007, 1, 100));
  rows.push_back(MakeStatementRow(0x1008, 1, 98));

  MockLineTable table(files, rows);

  // There are two exact matches for line 1.
  auto out = GetAllLineTableMatchesInUnit(table, "file1.cc", 1);
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ(LineMatch(0x1000, 1, 0), out[0]);
  EXPECT_EQ(LineMatch(0x1003, 1, 0), out[1]);

  // Searching for line 99 should catch both the 90->100 and the 95->100
  // transitions.
  out = GetAllLineTableMatchesInUnit(table, "file1.cc", 99);
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ(LineMatch(0x1005, 100, 0), out[0]);
  EXPECT_EQ(LineMatch(0x1007, 100, 0), out[1]);

  // Searching for something greater than 100 should fail.
  out = GetAllLineTableMatchesInUnit(table, "file1.cc", 101);
  EXPECT_TRUE(out.empty());
}

// Out-of-order lines. In this case there was some later code moved before
// the line being searched for, even though the transition of addresses
// goes in the opposite direction (high to low), we should find the line.
TEST(FindLine, GetAllLineTableMatchesInUnit_Reverse) {
  MockLineTable::FileNameVector files = {"file1.cc"};

  MockLineTable::RowVector rows;
  rows.push_back(MakeStatementRow(0x1000, 1, 105));  // Later code moved before.
  rows.push_back(MakeStatementRow(0x1001, 1, 101));  // Best match.
  rows.push_back(MakeStatementRow(0x1002, 1, 91));   //
  rows.push_back(MakeStatementRow(0x1003, 1, 103));  // Less-good match.

  MockLineTable table(files, rows);

  auto out = GetAllLineTableMatchesInUnit(table, "file1.cc", 100);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(LineMatch(0x1001, 101, 0), out[0]);
}

TEST(FindLine, GetBestLineMatches) {
  // Empty input.
  auto out = GetBestLineMatches({});
  EXPECT_TRUE(out.empty());

  // Should return the smallest line #.
  out = GetBestLineMatches({LineMatch(0x1000, 10, 0), LineMatch(0x1001, 7, 0),
                            LineMatch(0x1002, 100, 0)});
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(LineMatch(0x1001, 7, 0), out[0]);

  // When the smallest match has dupes, all should be returned assuming
  // the functions are different.
  out =
      GetBestLineMatches({LineMatch(0x1000, 10, 0), LineMatch(0x1001, 20, 1),
                          LineMatch(0x1002, 10, 2), LineMatch(0x1003, 30, 3)});
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ(LineMatch(0x1000, 10, 0), out[0]);
  EXPECT_EQ(LineMatch(0x1002, 10, 2), out[1]);

  // Dupes in the same function should return the smallest match.
  out = GetBestLineMatches(
      {LineMatch(0x1002, 10, 0),    // Match, discarded due to higher addr.
       LineMatch(0x1001, 20, 0),    // No line match.
       LineMatch(0x1000, 10, 0),    // Match, this one last lowest addr.
       LineMatch(0x1003, 10, 1)});  // Same line, different function.
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ(LineMatch(0x1000, 10, 0), out[0]);
  EXPECT_EQ(LineMatch(0x1003, 10, 1), out[1]);
}

}  // namespace zxdb
