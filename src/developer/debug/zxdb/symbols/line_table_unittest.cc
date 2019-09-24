// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/line_table.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/mock_line_table.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

// This test file covers the non-virtual helper functions on the LineTable object.

namespace zxdb {

TEST(LineTable, GetFirstRowIndexForAddress) {
  // This load address makes the addresses in the table start at 0x1000.
  SymbolContext context(0x1000);

  MockLineTable::FileNameVector files;
  files.push_back("file.cc");  // Name for file #1.

  MockLineTable empty_table(files, {});
  auto result = empty_table.GetFirstRowIndexForAddress(context, 0x1000);
  EXPECT_FALSE(result);  // Nothing is in the table.

  // This contains only odd addresses so even ones should be covered by the previous row.
  MockLineTable::RowVector rows;
  rows.push_back(MockLineTable::MakeStatementRow(0x1, 1, 2));
  rows.push_back(MockLineTable::MakeStatementRow(0x3, 1, 1));  // Dupe for File 1, line 1.
  rows.push_back(MockLineTable::MakeStatementRow(0x5, 1, 100));
  rows.push_back(MockLineTable::MakeStatementRow(0x5, 1, 100));  // Address dupe.
  rows.push_back(MockLineTable::MakeStatementRow(0x7, 1, 100));  // End marker, not a real addr.

  MockLineTable table(files, rows);

  result = table.GetFirstRowIndexForAddress(context, 0x1000);
  EXPECT_FALSE(result);  // Should be not found since it's before the table.

  result = table.GetFirstRowIndexForAddress(context, 0x1001);
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, *result);

  // Should be covered by the previous entry since there isn't a specific one for this address.
  result = table.GetFirstRowIndexForAddress(context, 0x1002);
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, *result);

  // Should find the first of the two dupes.
  result = table.GetFirstRowIndexForAddress(context, 0x1005);
  ASSERT_TRUE(result);
  EXPECT_EQ(2u, *result);

  // End of table.
  result = table.GetFirstRowIndexForAddress(context, 0x1007);
  ASSERT_TRUE(result);
  EXPECT_EQ(4u, *result);

  // Past the end of the table is covered by the last line.
  result = table.GetFirstRowIndexForAddress(context, 0x1008);
  ASSERT_TRUE(result);
  EXPECT_EQ(4u, *result);
  result = table.GetFirstRowIndexForAddress(context, 0x2000);
  ASSERT_TRUE(result);
  EXPECT_EQ(4u, *result);
}

}  // namespace zxdb
