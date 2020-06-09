// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/index_node.h"

#include <gtest/gtest.h>

#include "llvm/DebugInfo/DWARF/DWARFDie.h"

namespace zxdb {

using SymbolRef = IndexNode::SymbolRef;
using Kind = IndexNode::Kind;

// Tests de-duplicating type definitions, and upgrading forward declarations to full definitions.
TEST(IndexNode, DeDupeType) {
  IndexNode node(Kind::kType);

  // Type forward declaration should get appended.
  const uint32_t kFwdDecl1Offset = 20;
  node.AddDie(SymbolRef(IndexNode::SymbolRef::kDwarfDeclaration, kFwdDecl1Offset));
  ASSERT_EQ(1u, node.dies().size());
  EXPECT_EQ(kFwdDecl1Offset, node.dies()[0].offset());

  // Another forward declaration should be ignored in favor of the old one.
  const uint32_t kFwdDecl2Offset = 30;
  node.AddDie(SymbolRef(IndexNode::SymbolRef::kDwarfDeclaration, kFwdDecl2Offset));
  ASSERT_EQ(1u, node.dies().size());
  EXPECT_EQ(kFwdDecl1Offset, node.dies()[0].offset());

  // A full type definition should overwrite the forward declaration.
  const uint32_t kType1Offset = 40;
  node.AddDie(SymbolRef(IndexNode::SymbolRef::kDwarf, kType1Offset));
  ASSERT_EQ(1u, node.dies().size());
  EXPECT_EQ(kType1Offset, node.dies()[0].offset());

  // A duplicate full type definition should be ignored in favor of the old one.
  const uint32_t kType2Offset = 50;
  node.AddDie(SymbolRef(IndexNode::SymbolRef::kDwarf, kType2Offset));
  ASSERT_EQ(1u, node.dies().size());
  EXPECT_EQ(kType1Offset, node.dies()[0].offset());
}

TEST(IndexNode, DeDupeNamespace) {
  IndexNode root(Kind::kRoot);

  const char kName[] = "ns";

  // Add a namespace, it should be appended but no DIE stored (we don't bother storing DIEs for
  // namespaces).
  const uint32_t kNSOffset = 60;
  root.AddChild(Kind::kNamespace, kName, SymbolRef(IndexNode::SymbolRef::kDwarf, kNSOffset));
  ASSERT_EQ(1u, root.namespaces().size());
  EXPECT_TRUE(root.namespaces().begin()->second.dies().empty());

  // A duplicate namespace.
  root.AddChild(Kind::kNamespace, kName, SymbolRef(IndexNode::SymbolRef::kDwarf, kNSOffset));
  ASSERT_EQ(1u, root.namespaces().size());
  EXPECT_TRUE(root.namespaces().begin()->second.dies().empty());
}

}  // namespace zxdb
