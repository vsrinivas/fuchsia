// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/module_symbol_index_node.h"

#include "gtest/gtest.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"

namespace zxdb {

using DieRef = ModuleSymbolIndexNode::DieRef;
using RefType = ModuleSymbolIndexNode::RefType;

// Tests de-duplicating type definitions and namespaces, and upgrading forward
// declarations to full definitions.
TEST(ModuleSymbolIndexNode, DeDupe) {
  ModuleSymbolIndexNode node;

  // Add a function.
  const uint32_t kFunction1Offset = 10;
  node.AddDie(DieRef(RefType::kFunction, kFunction1Offset));

  // Type forward declaration should get appended.
  const uint32_t kFwdDecl1Offset = 20;
  node.AddDie(DieRef(RefType::kTypeDecl, kFwdDecl1Offset));
  ASSERT_EQ(2u, node.dies().size());
  EXPECT_EQ(kFunction1Offset, node.dies()[0].offset());
  EXPECT_EQ(kFwdDecl1Offset, node.dies()[1].offset());

  // Another forward declaration should be ignored in favor of the old one.
  const uint32_t kFwdDecl2Offset = 30;
  node.AddDie(DieRef(RefType::kTypeDecl, kFwdDecl2Offset));
  ASSERT_EQ(2u, node.dies().size());
  EXPECT_EQ(kFunction1Offset, node.dies()[0].offset());
  EXPECT_EQ(kFwdDecl1Offset, node.dies()[1].offset());

  // A full type definition should overwrite the forward declaration.
  const uint32_t kType1Offset = 40;
  node.AddDie(DieRef(RefType::kType, kType1Offset));
  ASSERT_EQ(2u, node.dies().size());
  EXPECT_EQ(kFunction1Offset, node.dies()[0].offset());
  EXPECT_EQ(kType1Offset, node.dies()[1].offset());

  // A duplicate full type definition should be ignored in favor of the old
  // one.
  const uint32_t kType2Offset = 50;
  node.AddDie(DieRef(RefType::kType, kType2Offset));
  ASSERT_EQ(2u, node.dies().size());
  EXPECT_EQ(kFunction1Offset, node.dies()[0].offset());
  EXPECT_EQ(kType1Offset, node.dies()[1].offset());

  // Add a namespace, it should be appended.
  const uint32_t kNS1Offset = 60;
  node.AddDie(DieRef(RefType::kNamespace, kNS1Offset));
  ASSERT_EQ(3u, node.dies().size());
  EXPECT_EQ(kFunction1Offset, node.dies()[0].offset());
  EXPECT_EQ(kType1Offset, node.dies()[1].offset());
  EXPECT_EQ(kNS1Offset, node.dies()[2].offset());

  // A duplicate namespace should be ignored in favor of the old one.
  const uint32_t kNS2Offset = 70;
  node.AddDie(DieRef(RefType::kNamespace, kNS2Offset));
  ASSERT_EQ(3u, node.dies().size());
  EXPECT_EQ(kFunction1Offset, node.dies()[0].offset());
  EXPECT_EQ(kType1Offset, node.dies()[1].offset());
  EXPECT_EQ(kNS1Offset, node.dies()[2].offset());

  // A variable should be appended.
  const uint32_t kVar1Offset = 80;
  node.AddDie(DieRef(RefType::kVariable, kVar1Offset));
  ASSERT_EQ(4u, node.dies().size());
  EXPECT_EQ(kFunction1Offset, node.dies()[0].offset());
  EXPECT_EQ(kType1Offset, node.dies()[1].offset());
  EXPECT_EQ(kNS1Offset, node.dies()[2].offset());
  EXPECT_EQ(kVar1Offset, node.dies()[3].offset());

  // Duplicate function and variable should be appended.
  const uint32_t kFunction2Offset = 90;
  const uint32_t kVar2Offset = 100;
  node.AddDie(DieRef(RefType::kFunction, kFunction2Offset));
  node.AddDie(DieRef(RefType::kVariable, kVar2Offset));
  ASSERT_EQ(6u, node.dies().size());
  EXPECT_EQ(kFunction1Offset, node.dies()[0].offset());
  EXPECT_EQ(kType1Offset, node.dies()[1].offset());
  EXPECT_EQ(kNS1Offset, node.dies()[2].offset());
  EXPECT_EQ(kVar1Offset, node.dies()[3].offset());
  EXPECT_EQ(kFunction2Offset, node.dies()[4].offset());
  EXPECT_EQ(kVar2Offset, node.dies()[5].offset());
}

// Tests AddChild() and its merging capabilities when a duplicate is found.
TEST(ModuleSymbolIndexNode, AddChildMerge) {
  const uint32_t offset1 = 10;
  const uint32_t offset2 = 20;
  const uint32_t offset3 = 30;
  const uint32_t offset4 = 40;

  const std::string foo("foo");
  const std::string bar("bar");
  const std::string bloop("bloop");

  // The root has the hierarchy:
  //   [root]
  //     node1 = "foo" [1 function = #1]
  //       node2 = "bar" [1 function = #2]
  ModuleSymbolIndexNode node2;
  node2.AddDie(DieRef(RefType::kFunction, offset2));

  ModuleSymbolIndexNode node1;
  node1.AddDie(DieRef(RefType::kFunction, offset1));
  node1.AddChild(bar, std::move(node2));

  ModuleSymbolIndexNode root;
  EXPECT_TRUE(root.empty());
  root.AddChild(foo, std::move(node1));
  EXPECT_FALSE(root.empty());

  // The merged one has the hierarchy:
  //   merge1 = "foo" [1 function = #3]
  //     merge2 = "bloop" [1 function = #4]
  ModuleSymbolIndexNode merge2;
  merge2.AddDie(DieRef(RefType::kFunction, offset4));

  ModuleSymbolIndexNode merge1;
  merge1.AddDie(DieRef(RefType::kFunction, offset3));
  merge1.AddChild(bloop, std::move(merge2));

  // Now merge in "merge1" as a child of the root.
  root.AddChild(foo, std::move(merge1));

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
