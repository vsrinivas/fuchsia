// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/module_symbol_index_node.h"

#include "gtest/gtest.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"

namespace zxdb {

// Tests AddChild() and its merging capabilities when a duplicate is found.
TEST(ModuleSymbolIndexNode, AddChildMerge) {
  const uint32_t offset1 = 10;
  const uint32_t offset2 = 20;
  const uint32_t offset3 = 30;
  const uint32_t offset4 = 40;

  const std::string foo("foo");
  const std::string bar("bar");
  const std::string bloop("bloop");

  using DieRef = ModuleSymbolIndexNode::DieRef;

  // The root has the hierarchy:
  //   [root]
  //     node1 = "foo" [1 function = #1]
  //       node2 = "bar" [1 function = #2]
  ModuleSymbolIndexNode node2;
  node2.AddDie(DieRef(offset2));

  ModuleSymbolIndexNode node1;
  node1.AddDie(DieRef(offset1));
  node1.AddChild(std::make_pair(bar, std::move(node2)));

  ModuleSymbolIndexNode root;
  EXPECT_TRUE(root.empty());
  root.AddChild(std::make_pair(foo, std::move(node1)));
  EXPECT_FALSE(root.empty());

  // The merged one has the hierarchy:
  //   merge1 = "foo" [1 function = #3]
  //     merge2 = "bloop" [1 function = #4]
  ModuleSymbolIndexNode merge2;
  merge2.AddDie(DieRef(offset4));

  ModuleSymbolIndexNode merge1;
  merge1.AddDie(DieRef(offset3));
  merge1.AddChild(std::make_pair(bloop, std::move(merge2)));

  // Now merge in "merge1" as a child of the root.
  root.AddChild(std::make_pair(foo, std::move(merge1)));

  // This should merge the two to get:
  //   [root]
  //     out1 = "foo" [2 functions = #1, #3]
  //       out2 = "bar" [1 function = #2]
  //       out3 = "bloop" [1 function = #4]

  // Check root.
  ASSERT_EQ(1u, root.sub().size());
  EXPECT_FALSE(root.empty());
  EXPECT_TRUE(root.dies().empty());
  EXPECT_EQ(foo, root.sub().begin()->first);

  // Check out1.
  const ModuleSymbolIndexNode& out1 = root.sub().begin()->second;
  ASSERT_EQ(2u, out1.dies().size());
  EXPECT_EQ(offset1, out1.dies()[0].offset());
  EXPECT_EQ(offset3, out1.dies()[1].offset());
  ASSERT_EQ(2u, out1.sub().size());
  EXPECT_EQ(bar, out1.sub().begin()->first);
  EXPECT_EQ(bloop, (++out1.sub().begin())->first);

  // Check out2.
  const ModuleSymbolIndexNode& out2 = out1.sub().begin()->second;
  EXPECT_TRUE(out2.sub().empty());
  ASSERT_EQ(1u, out2.dies().size());
  EXPECT_EQ(offset2, out2.dies()[0].offset());

  // Check out3.
  const ModuleSymbolIndexNode& out3 = (++out1.sub().begin())->second;
  EXPECT_TRUE(out3.sub().empty());
  ASSERT_EQ(1u, out3.dies().size());
  EXPECT_EQ(offset4, out3.dies()[0].offset());
}

}  // namespace zxdb
