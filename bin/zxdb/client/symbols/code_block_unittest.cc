// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/code_block.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(CodeBlock, ContainsAddress) {
  auto block = fxl::MakeRefCounted<CodeBlock>(Symbol::kTagLexicalBlock);

  // No code range: contains all addresses.
  EXPECT_TRUE(block->ContainsAddress(0));
  EXPECT_TRUE(block->ContainsAddress(0x2000));

  // Set some ranges.
  CodeBlock::CodeRanges ranges;
  ranges.emplace_back(0x1000, 0x2000);
  ranges.emplace_back(0x3000, 0x3001);
  block->set_code_ranges(ranges);

  // Blocks should count the beginning but not the end as inside them.
  EXPECT_TRUE(block->ContainsAddress(0x1000));
  EXPECT_TRUE(block->ContainsAddress(0x1100));
  EXPECT_FALSE(block->ContainsAddress(0x2000));

  EXPECT_FALSE(block->ContainsAddress(0x2fff));
  EXPECT_TRUE(block->ContainsAddress(0x3000));
  EXPECT_FALSE(block->ContainsAddress(0x3001));
  EXPECT_FALSE(block->ContainsAddress(0x3002));
}

TEST(CodeBlock, GetMostSpecificChild) {
  auto outer = fxl::MakeRefCounted<CodeBlock>(Symbol::kTagLexicalBlock);

  // Outer has two ranges.
  CodeBlock::CodeRanges ranges;
  ranges.emplace_back(0x1000, 0x2000);
  ranges.emplace_back(0x3000, 0x3001);
  outer->set_code_ranges(ranges);

  // There are two inner blocks, one covers partially the first range, the
  // other covers exactly the second range.
  auto first_child = fxl::MakeRefCounted<CodeBlock>(Symbol::kTagLexicalBlock);
  ranges.clear();
  ranges.emplace_back(0x1000, 0x2000);
  first_child->set_code_ranges(ranges);

  auto second_child = fxl::MakeRefCounted<CodeBlock>(Symbol::kTagLexicalBlock);
  ranges.clear();
  ranges.emplace_back(0x3000, 0x3001);
  second_child->set_code_ranges(ranges);

  // Append the child ranges.
  std::vector<LazySymbol> outer_inner;
  outer_inner.emplace_back(first_child);
  outer_inner.emplace_back(second_child);
  outer->set_inner_blocks(outer_inner);

  // The first child has yet another child.
  auto child_child = fxl::MakeRefCounted<CodeBlock>(Symbol::kTagLexicalBlock);
  ranges.clear();
  ranges.emplace_back(0x1000, 0x1100);
  child_child->set_code_ranges(ranges);
  std::vector<LazySymbol> inner_inner;
  inner_inner.emplace_back(child_child);
  first_child->set_inner_blocks(inner_inner);

  // The second child has an inner child with no defined range.
  auto child_child2 = fxl::MakeRefCounted<CodeBlock>(Symbol::kTagLexicalBlock);
  std::vector<LazySymbol> inner_inner2;
  inner_inner2.emplace_back(child_child2);
  second_child->set_inner_blocks(inner_inner2);

  // Querying for something out-of-range.
  EXPECT_EQ(nullptr, outer->GetMostSpecificChild(0x1));

  // Something in the first level of children but not in the second.
  EXPECT_EQ(first_child.get(), outer->GetMostSpecificChild(0x1200));

  // Lowest level of child.
  EXPECT_EQ(child_child.get(), outer->GetMostSpecificChild(0x1000));
  EXPECT_EQ(child_child2.get(), outer->GetMostSpecificChild(0x3000));
}

}  // namespace zxdb
