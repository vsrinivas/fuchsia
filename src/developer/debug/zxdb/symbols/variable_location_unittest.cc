// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/variable_location.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

TEST(VariableLocation, EntryInRange) {
  VariableLocation::Entry entry;

  SymbolContext context = SymbolContext::ForRelativeAddresses();

  // Default should be 0 beginning and end which always in range.
  EXPECT_TRUE(entry.InRange(context, 0));
  EXPECT_TRUE(entry.InRange(context, 1));
  EXPECT_TRUE(entry.InRange(context, static_cast<uint64_t>(-1)));

  // Empty range not starting at 0 is never valid.
  entry.begin = 1;
  entry.end = 1;
  EXPECT_FALSE(entry.InRange(context, 0));
  EXPECT_FALSE(entry.InRange(context, 1));
  EXPECT_FALSE(entry.InRange(context, 2));

  // Normal range. Beginning is inclusive, ending is exclusive.
  entry.begin = 0x10;
  entry.end = 0x20;
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
  // Valid from 0x10-0x20 and 0x30-0x40
  std::vector<VariableLocation::Entry> entries;
  entries.resize(2);
  entries[0].begin = 0x10;
  entries[0].end = 0x20;
  entries[1].begin = 0x30;
  entries[1].end = 0x40;

  VariableLocation loc(entries);

  SymbolContext context = SymbolContext::ForRelativeAddresses();

  const VariableLocation::Entry* entry = loc.EntryForIP(context, 0);
  EXPECT_FALSE(entry);  // Not found.

  entry = loc.EntryForIP(context, 0x10);
  EXPECT_TRUE(entry);
  EXPECT_EQ(0x10u, entry->begin);
  EXPECT_EQ(0x20u, entry->end);

  entry = loc.EntryForIP(context, 0x1f);
  EXPECT_TRUE(entry);

  entry = loc.EntryForIP(context, 0x20);
  EXPECT_FALSE(entry);

  entry = loc.EntryForIP(context, 0x30);
  EXPECT_TRUE(entry);
  EXPECT_EQ(0x30u, entry->begin);
  EXPECT_EQ(0x40u, entry->end);

  entry = loc.EntryForIP(context, 0x40);
  EXPECT_FALSE(entry);
}

}  // namespace zxdb
