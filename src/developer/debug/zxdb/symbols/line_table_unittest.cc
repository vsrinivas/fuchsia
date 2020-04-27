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
  EXPECT_EQ(0x1u, result.sequence[result.index].Address);

  // Valid query for sequence added later.
  result = table.GetRowForAddress(context, 0x1104);
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(1u, result.index);                               // Index within newer sequence.
  EXPECT_EQ(0x103u, result.sequence[result.index].Address);  // Line before queried addr.

  // Query in between the two valid sequences
  result = table.GetRowForAddress(context, 0x1080);
  EXPECT_TRUE(result.empty());  // Should be not found.
}

}  // namespace zxdb
