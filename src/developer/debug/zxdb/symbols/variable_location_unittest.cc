// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/variable_location.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

TEST(VariableLocation, EntryInRange) {
  VariableLocation::Entry entry;

  SymbolContext context = SymbolContext::ForRelativeAddresses();

  // Default should be 0 beginning and end which never in range.
  EXPECT_FALSE(entry.InRange(context, 0));
  EXPECT_FALSE(entry.InRange(context, 1));
  EXPECT_FALSE(entry.InRange(context, static_cast<uint64_t>(-1)));

  // Normal range. Beginning is inclusive, ending is exclusive.
  entry.range = AddressRange(0x10, 0x20);
  EXPECT_FALSE(entry.InRange(context, 0));
  EXPECT_FALSE(entry.InRange(context, 0xf));
  EXPECT_TRUE(entry.InRange(context, 0x10));
  EXPECT_TRUE(entry.InRange(context, 0x11));
  EXPECT_TRUE(entry.InRange(context, 0x1f));
  EXPECT_FALSE(entry.InRange(context, 0x20));
  EXPECT_FALSE(entry.InRange(context, 0x21));

  // Test a module loaded at 0x1000 does the right thing with offset addresses.
  context = SymbolContext(0x1000);
  // Value in-range above is no longer valid with the offset symbol context.
  EXPECT_FALSE(entry.InRange(context, 0x10));
  // Same tests as above offset by 0x1000.
  EXPECT_FALSE(entry.InRange(context, 0x100f));
  EXPECT_TRUE(entry.InRange(context, 0x1010));
  EXPECT_TRUE(entry.InRange(context, 0x1011));
  EXPECT_TRUE(entry.InRange(context, 0x101f));
  EXPECT_FALSE(entry.InRange(context, 0x1020));
  EXPECT_FALSE(entry.InRange(context, 0x1021));
}

TEST(VariableLocation, EntryForIP) {
  // These fake DWARF expressions define each location. They're just random data rather than a valid
  // expression.
  std::vector<uint8_t> expr1{0x01};
  std::vector<uint8_t> expr2{0x02};
  std::vector<uint8_t> expr3{0x03};

  // Valid from 0x10-0x20 and 0x30-0x40
  std::vector<VariableLocation::Entry> entries;
  entries.resize(2);
  entries[0].range = AddressRange(0x10, 0x20);
  entries[0].expression = DwarfExpr(expr1);
  entries[1].range = AddressRange(0x30, 0x40);
  entries[1].expression = DwarfExpr(expr2);

  VariableLocation loc(entries);

  SymbolContext context = SymbolContext::ForRelativeAddresses();

  const DwarfExpr* expr = loc.ExprForIP(context, 0);
  EXPECT_FALSE(expr);  // Not found.

  expr = loc.ExprForIP(context, 0x10);
  ASSERT_TRUE(expr);
  EXPECT_EQ(expr1, expr->data());

  expr = loc.ExprForIP(context, 0x1f);
  EXPECT_TRUE(expr);

  expr = loc.ExprForIP(context, 0x20);
  EXPECT_FALSE(expr);

  expr = loc.ExprForIP(context, 0x30);
  ASSERT_TRUE(expr);
  EXPECT_EQ(expr2, expr->data());

  expr = loc.ExprForIP(context, 0x40);
  EXPECT_FALSE(expr);

  // Test assignment and now provide a VariableLocation with a default.
  loc = VariableLocation(entries, DwarfExpr(expr3));

  // The found ranges should still be found.
  expr = loc.ExprForIP(context, 0x10);
  ASSERT_TRUE(expr);
  EXPECT_EQ(expr1, expr->data());

  // But now previously-unmatched ranges will return the default.
  expr = loc.ExprForIP(context, 0x28);
  ASSERT_TRUE(expr);
  EXPECT_EQ(expr3, expr->data());

  // Test the single-default-location constructor.
  loc = VariableLocation(DwarfExpr(expr3));
  expr = loc.ExprForIP(context, 0x28);
  ASSERT_TRUE(expr);
  EXPECT_EQ(expr3, expr->data());
}

}  // namespace zxdb
