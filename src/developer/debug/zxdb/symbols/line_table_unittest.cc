// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/line_table.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/symbols/mock_line_table.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

// This test file covers the non-virtual helper functions on the LineTable object.

namespace zxdb {

TEST(LineTable, GetRowForAddress) {
  // This load address makes the addresses in the table start at 0x1000.
  SymbolContext context(0x1000);

  MockLineTable::FileNameVector files;
  files.push_back("file.cc");  // Name for file #1.

  MockLineTable empty_table(files, {});
  auto result = empty_table.GetRowForAddress(context, 0x1000);
  EXPECT_TRUE(result.empty());  // Nothing is in the table.

  // This contains only odd addresses so even ones should be covered by the previous row.
  MockLineTable::RowVector rows;
  rows.push_back(MockLineTable::MakeStatementRow(0x1, 1, 2));
  rows.push_back(MockLineTable::MakeStatementRow(0x3, 1, 1));  // Dupe for File 1, line 1.
  rows.push_back(MockLineTable::MakeStatementRow(0x5, 1, 100));
  rows.push_back(MockLineTable::MakeStatementRow(0x5, 1, 100));  // Address dupe.
  // No end sequence marker.

  MockLineTable table(files, rows);

  // Without an end sequence marker, the rows won't be found.
  result = table.GetRowForAddress(context, 0x1001);
  EXPECT_TRUE(result.empty());  // Nothing is in the table.

  // Make one with an end sequence marker, it should become valid.
  rows.push_back(MockLineTable::MakeEndSequenceRow(0x7, 1, 100));  // End marker, not a real addr.
  table = MockLineTable(files, rows);

  result = table.GetRowForAddress(context, 0x1000);
  EXPECT_TRUE(result.empty());  // Should be not found since it's before the table.

  result = table.GetRowForAddress(context, 0x1001);
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(5u, result.sequence.size());
  EXPECT_EQ(0u, result.index);

  // Should be covered by the previous entry since there isn't a specific one for this address.
  result = table.GetRowForAddress(context, 0x1002);
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(5u, result.sequence.size());
  EXPECT_EQ(0u, result.index);

  // Should find the first of the two dupes.
  result = table.GetRowForAddress(context, 0x1005);
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(5u, result.sequence.size());
  EXPECT_EQ(2u, result.index);

  // End of table.
  result = table.GetRowForAddress(context, 0x1007);
  ASSERT_TRUE(result.empty());

  // Add some rows corresponding to stripped code (sequence starting @ 0). Note that this overlaps
  // the previous valid sequence and this one should be ignored.
  rows.push_back(MockLineTable::MakeStatementRow(0x0, 1, 100));
  rows.push_back(MockLineTable::MakeStatementRow(0x0, 1, 101));
  rows.push_back(MockLineTable::MakeEndSequenceRow(0x0, 1, 101));

  // Add some more valid rows. These ones are added to the beginning so the initial table is
  // out-of-order.
  MockLineTable::RowVector new_rows;
  new_rows.push_back(MockLineTable::MakeStatementRow(0x101, 1, 2));
  new_rows.push_back(MockLineTable::MakeStatementRow(0x103, 1, 1));  // Dupe for File 1, line 1.
  new_rows.push_back(MockLineTable::MakeStatementRow(0x105, 1, 100));
  new_rows.push_back(MockLineTable::MakeEndSequenceRow(0x107, 1, 100));

  rows.insert(rows.begin(), new_rows.begin(), new_rows.end());
  table = MockLineTable(files, rows);

  // Address 0 should still be not found (the sequence starting @ 0 should be considered stripped).
  result = table.GetRowForAddress(context, 0x1000);
  EXPECT_TRUE(result.empty());  // Should be not found since it's before the table.

  // Valid query from above.
  result = table.GetRowForAddress(context, 0x1001);
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(0u, result.index);
  EXPECT_EQ(0x1u, result.sequence[result.index].Address.Address);

  // Valid query for sequence added later.
  result = table.GetRowForAddress(context, 0x1104);
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(1u, result.index);                                       // Index within newer sequence.
  EXPECT_EQ(0x103u, result.sequence[result.index].Address.Address);  // Line before queried addr.

  // Query in between the two valid sequences
  result = table.GetRowForAddress(context, 0x1080);
  EXPECT_TRUE(result.empty());  // Should be not found.
}

// Tests handling for addresses matching "line 0" entries (indicating compiler-generated).
TEST(LineTable, GetRowForAddress_Line0) {
  // This load address makes the addresses in the table start at 0x1000.
  SymbolContext context(0x1000);

  MockLineTable::FileNameVector files;
  files.push_back("file.cc");  // Name for file #1.

  MockLineTable::RowVector rows;
  rows.push_back(MockLineTable::MakeStatementRow(0x1, 1, 2));
  rows.push_back(MockLineTable::MakeStatementRow(0x3, 1, 0));  // <- Compiler-generated.
  rows.push_back(MockLineTable::MakeStatementRow(0x5, 1, 3));
  rows.push_back(MockLineTable::MakeStatementRow(0x7, 1, 0));  // <- Compiler-generated
  // The settings for the "end sequence" marker aren't really used. They're normally inherited from
  // the previous line because of the way the data is encoded in DWARF using a state machine. We
  // inherit the same file/line from the previous entry in this same way.
  rows.push_back(MockLineTable::MakeEndSequenceRow(0x9, 1, 0));

  MockLineTable table(files, rows);

  // Exact match query for the "line 0" address should return it.
  auto result = table.GetRowForAddress(context, 0x1003, LineTable::kExactMatch);
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(0x3u, result.get().Address.Address);
  EXPECT_EQ(0u, result.get().Line);

  // Exact match query for the address immediately following 0x7 should also be covered by that
  // entry.
  result = table.GetRowForAddress(context, 0x1008, LineTable::kExactMatch);
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(0x7u, result.get().Address.Address);
  EXPECT_EQ(0u, result.get().Line);

  // Querying the first "line 0" entry with skipping should yield the next address.
  result = table.GetRowForAddress(context, 0x1003, LineTable::kSkipCompilerGenerated);
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(0x5u, result.get().Address.Address);
  EXPECT_EQ(3u, result.get().Line);

  // Query the address immediately following 0x7. Since this is the last real entry in the table
  // (the end sequence marker doesn't count), the line should not be advanced and the "line 0"
  // entry should be returned.
  result = table.GetRowForAddress(context, 0x1008, LineTable::kSkipCompilerGenerated);
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(0x7u, result.get().Address.Address);
  EXPECT_EQ(0u, result.get().Line);
}

// Tests that when an address matches two lines, one an EndSequence and one a good match, that the
// second one is returned. This construct can appear on function boundaries when there is no padding
// between them. The EndSequece marker is non-inclusive, it just marks the end of the previous
// function.
TEST(LineTable, GetRowForAddress_EndSequence) {
  SymbolContext context(0x1000);

  MockLineTable::FileNameVector files;
  files.push_back("file.cc");  // Name for file #1.

  MockLineTable::RowVector rows;
  rows.push_back(MockLineTable::MakeStatementRow(0x4, 1, 2));
  rows.push_back(MockLineTable::MakeEndSequenceRow(0x8, 1, 2));
  rows.push_back(MockLineTable::MakeStatementRow(0x8, 1, 3));  // <- Duplicate address.
  rows.push_back(MockLineTable::MakeStatementRow(0xa, 1, 4));
  rows.push_back(MockLineTable::MakeEndSequenceRow(0xa, 1, 4));

  MockLineTable table(files, rows);
  auto found = table.GetRowForAddress(context, 0x1008, LineTable::kSkipCompilerGenerated);
  ASSERT_FALSE(found.empty());
  EXPECT_EQ(0x8u, found.get().Address.Address);
  EXPECT_EQ(3u, found.get().Line);  // This is the non-end-sequence row at this address.
}

}  // namespace zxdb
