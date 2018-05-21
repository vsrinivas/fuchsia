// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/module_symbol_index_node.h"

#include "gtest/gtest.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"

namespace zxdb {

namespace {

// You need a DWARFUnit to make a DWARFDie, but making a DWARFUnit is involved
// and we don't actually need to dereferece anything. This function returns
// a fake pointer that can be used.
llvm::DWARFUnit* GetFakeUnitPtr() {
  static int fake_value = 0;
  return reinterpret_cast<llvm::DWARFUnit*>(&fake_value);
}

}  // namespace

// Tests AddChild() and its merging capabilities when a duplicate is found.
TEST(ModuleSymbolIndexNode, AddChildMerge) {
  llvm::DWARFDebugInfoEntry entry1;
  llvm::DWARFDebugInfoEntry entry2;
  llvm::DWARFDebugInfoEntry entry3;
  llvm::DWARFDebugInfoEntry entry4;

  llvm::DWARFDie die1(GetFakeUnitPtr(), &entry1);
  llvm::DWARFDie die2(GetFakeUnitPtr(), &entry2);
  llvm::DWARFDie die3(GetFakeUnitPtr(), &entry3);
  llvm::DWARFDie die4(GetFakeUnitPtr(), &entry4);

  const std::string foo("foo");
  const std::string bar("bar");
  const std::string bloop("bloop");

  // The root has the hierarchy:
  //   [root]
  //     node1 = "foo" [1 function = die1]
  //       node2 = "bar" [1 function = die2]
  ModuleSymbolIndexNode node2;
  node2.AddFunctionDie(die2);

  ModuleSymbolIndexNode node1;
  node1.AddFunctionDie(die1);
  node1.AddChild(std::make_pair(bar, std::move(node2)));

  ModuleSymbolIndexNode root;
  EXPECT_TRUE(root.empty());
  root.AddChild(std::make_pair(foo, std::move(node1)));
  EXPECT_FALSE(root.empty());

  // The merged one has the hierarchy:
  //   merge1 = "foo" [1 function = die3]
  //     merge2 = "bloop" [1 function = die4]
  ModuleSymbolIndexNode merge2;
  merge2.AddFunctionDie(die4);

  ModuleSymbolIndexNode merge1;
  merge1.AddFunctionDie(die3);
  merge1.AddChild(std::make_pair(bloop, std::move(merge2)));

  // Now merge in "merge1" as a child of the root.
  root.AddChild(std::make_pair(foo, std::move(merge1)));

  // This should merge the two to get:
  //   [root]
  //     out1 = "foo" [2 functions = die1, die3]
  //       out2 = "bar" [1 function = die2]
  //       out3 = "bloop" [1 function = die4]

  // Check root.
  ASSERT_EQ(1u, root.sub().size());
  EXPECT_FALSE(root.empty());
  EXPECT_TRUE(root.function_dies().empty());
  EXPECT_EQ(foo, root.sub().begin()->first);

  // Check out1.
  const ModuleSymbolIndexNode& out1 = root.sub().begin()->second;
  ASSERT_EQ(2u, out1.function_dies().size());
  EXPECT_EQ(die1, out1.function_dies()[0]);
  EXPECT_EQ(die3, out1.function_dies()[1]);
  ASSERT_EQ(2u, out1.sub().size());
  EXPECT_EQ(bar, out1.sub().begin()->first);
  EXPECT_EQ(bloop, (++out1.sub().begin())->first);

  // Check out2.
  const ModuleSymbolIndexNode& out2 = out1.sub().begin()->second;
  EXPECT_TRUE(out2.sub().empty());
  ASSERT_EQ(1u, out2.function_dies().size());
  EXPECT_EQ(die2, out2.function_dies()[0]);

  // Check out3.
  const ModuleSymbolIndexNode& out3 = (++out1.sub().begin())->second;
  EXPECT_TRUE(out3.sub().empty());
  ASSERT_EQ(1u, out3.function_dies().size());
  EXPECT_EQ(die4, out3.function_dies()[0]);
}

}  // namespace zxdb
