// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/code_block.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/symbols/call_site.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

TEST(CodeBlock, ContainsAddress) {
  auto block = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);

  SymbolContext context = SymbolContext::ForRelativeAddresses();

  // No code range: contains no addresses.
  EXPECT_FALSE(block->ContainsAddress(context, 0));
  EXPECT_FALSE(block->ContainsAddress(context, 0x2000));

  // Set some ranges.
  block->set_code_ranges(AddressRanges(
      AddressRanges::kCanonical, {AddressRange(0x1000, 0x2000), AddressRange(0x3000, 0x3001)}));

  // Blocks should count the beginning but not the end as inside them.
  EXPECT_TRUE(block->ContainsAddress(context, 0x1000));
  EXPECT_TRUE(block->ContainsAddress(context, 0x1100));
  EXPECT_FALSE(block->ContainsAddress(context, 0x2000));

  EXPECT_FALSE(block->ContainsAddress(context, 0x2fff));
  EXPECT_TRUE(block->ContainsAddress(context, 0x3000));
  EXPECT_FALSE(block->ContainsAddress(context, 0x3001));
  EXPECT_FALSE(block->ContainsAddress(context, 0x3002));

  // Test with a non-relative symbol context.
  constexpr uint64_t kBase = 0x10000000;
  SymbolContext base_context(kBase);
  EXPECT_FALSE(block->ContainsAddress(base_context, 0x1000));
  EXPECT_TRUE(block->ContainsAddress(base_context, kBase + 0x1000));
}

TEST(CodeBlock, GetMostSpecificChild) {
  auto outer = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);

  // Outer has two ranges.
  outer->set_code_ranges(AddressRanges(
      AddressRanges::kCanonical, {AddressRange(0x1000, 0x2000), AddressRange(0x3000, 0x3001)}));

  // There are two inner blocks, one covers partially the first range, the other covers exactly the
  // second range.
  auto first_child = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  first_child->set_code_ranges(AddressRanges(AddressRange(0x1000, 0x2000)));

  auto second_child = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  second_child->set_code_ranges(AddressRanges(AddressRange(0x3000, 0x3001)));

  // Append the child ranges.
  std::vector<LazySymbol> outer_inner;
  outer_inner.emplace_back(first_child);
  outer_inner.emplace_back(second_child);
  outer->set_inner_blocks(outer_inner);

  // The first child has yet another child.
  auto child_child = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  child_child->set_code_ranges(AddressRanges(AddressRange(0x1000, 0x1100)));
  std::vector<LazySymbol> inner_inner;
  inner_inner.emplace_back(child_child);
  first_child->set_inner_blocks(inner_inner);

  // The first child's child has an inlined subroutine child.
  auto child_child_inline = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kInlinedSubroutine);
  child_child_inline->set_code_ranges(AddressRanges(AddressRange(0x1020, 0x1030)));
  child_child->set_inner_blocks(std::vector<LazySymbol>{child_child_inline});

  // The second child has an inner child with no defined range. This should not get used.
  auto child_child2 = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  std::vector<LazySymbol> inner_inner2;
  inner_inner2.emplace_back(child_child2);
  second_child->set_inner_blocks(inner_inner2);

  SymbolContext context = SymbolContext::ForRelativeAddresses();

  // Querying for something out-of-range.
  EXPECT_EQ(nullptr, outer->GetMostSpecificChild(context, 0x1));

  // Something in the first level of children but not in the second.
  EXPECT_EQ(first_child.get(), outer->GetMostSpecificChild(context, 0x1200));

  // Lowest level of child.
  EXPECT_EQ(child_child.get(), outer->GetMostSpecificChild(context, 0x1000));
  EXPECT_EQ(second_child.get(), outer->GetMostSpecificChild(context, 0x3000));

  // Querying for something in the inlined routine is controlled by the optional flag.
  EXPECT_EQ(child_child_inline.get(), outer->GetMostSpecificChild(context, 0x1020, true));
  EXPECT_EQ(child_child.get(), outer->GetMostSpecificChild(context, 0x1020, false));
}

TEST(CodeBlock, GetAmbiguousInlineChain) {
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();
  constexpr TargetPointer kAddress = 0x1000;

  // Outer physical (non-inline) function.
  auto outer = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  outer->set_code_ranges(AddressRanges(AddressRange(kAddress, kAddress + 0x1000)));

  // Middle inline function starting at the same address.
  auto middle = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  middle->set_code_ranges(AddressRanges(AddressRange(kAddress, kAddress + 0x100)));
  SymbolTestContainingBlockSetter middle_parent(middle, outer);

  // Inner inline function is the most specific (smallest) one.
  auto inner = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  inner->set_code_ranges(AddressRanges(AddressRange(kAddress, kAddress + 0x10)));
  SymbolTestContainingBlockSetter inner_parent(inner, middle);

  // Given a non-inline address, the ambiguous inline chain should only return the function itself.
  auto result = inner->GetAmbiguousInlineChain(symbol_context, kAddress + 1);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ(inner.get(), result[0].get());

  // Test the same condition using the outer frame.
  result = outer->GetAmbiguousInlineChain(symbol_context, kAddress + 1);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ(outer.get(), result[0].get());

  // Test the ambiguous address, it should give all 3.
  result = inner->GetAmbiguousInlineChain(symbol_context, kAddress);
  ASSERT_EQ(3u, result.size());
  EXPECT_EQ(inner.get(), result[0].get());
  EXPECT_EQ(middle.get(), result[1].get());
  EXPECT_EQ(outer.get(), result[2].get());
}

TEST(CodeBlock, GetContainingFunction) {
  // Innermost scope.
  auto lexical_block = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);

  // The inline function wraps the lexical block.
  auto inline_fn = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  SymbolTestParentSetter lexical_parent_setter(lexical_block, inline_fn);

  // Collection to serve as the parent for the inline function.
  auto collection = MakeCollectionType(DwarfTag::kClassType, "Class", {});
  SymbolTestParentSetter inline_parent_setter(inline_fn, collection);

  // Physical function containing the inline. This relationship uses the "containing block" and not
  // the parent (which is the class set above).
  auto physical_fn = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  SymbolTestContainingBlockSetter inline_containing_setter(inline_fn, physical_fn);

  EXPECT_EQ(inline_fn.get(),
            lexical_block->GetContainingFunction(CodeBlock::kInlineOrPhysical).get());
  EXPECT_EQ(physical_fn.get(),
            lexical_block->GetContainingFunction(CodeBlock::kPhysicalOnly).get());
  EXPECT_EQ(physical_fn.get(), physical_fn->GetContainingFunction().get());
}

TEST(CodeBlock, GetCallSiteForReturnTo) {
  auto outer = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);

  // Outer has two ranges.
  outer->set_code_ranges(AddressRanges(
      AddressRanges::kCanonical, {AddressRange(0x1000, 0x2000), AddressRange(0x3000, 0x3001)}));

  // There are two inner blocks, one covers partially the first range, the other covers exactly the
  // second range.
  auto first_child = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  first_child->set_code_ranges(AddressRanges(AddressRange(0x1000, 0x2000)));

  auto second_child = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  second_child->set_code_ranges(AddressRanges(AddressRange(0x3000, 0x3001)));

  // Append the child ranges.
  std::vector<LazySymbol> outer_inner;
  outer_inner.emplace_back(first_child);
  outer_inner.emplace_back(second_child);
  outer->set_inner_blocks(outer_inner);

  // The first child has two call sites. These are added in backwards order since DWARF doesn't
  // guarantee any ordering.
  constexpr uint64_t kFirstChildReturnAddress1 = 0x1011;
  auto first_child_cs1 =
      fxl::MakeRefCounted<CallSite>(kFirstChildReturnAddress1, std::vector<LazySymbol>{});
  constexpr uint64_t kFirstChildReturnAddress2 = 0x1021;
  auto first_child_cs2 =
      fxl::MakeRefCounted<CallSite>(kFirstChildReturnAddress2, std::vector<LazySymbol>{});
  first_child->set_call_sites({LazySymbol(first_child_cs2), LazySymbol(first_child_cs1)});

  // The second child has one call site at the end returning to the instruction following it.
  constexpr uint64_t kSecondChildReturnAddress = 0x3001;
  auto second_child_cs =
      fxl::MakeRefCounted<CallSite>(kSecondChildReturnAddress, std::vector<LazySymbol>{});
  second_child->set_call_sites({LazySymbol(second_child_cs)});

  constexpr uint64_t kLoadAddress = 0x100000000;
  SymbolContext symbol_context(kLoadAddress);

  // Outside of any range.
  EXPECT_EQ(nullptr, outer->GetCallSiteForReturnTo(symbol_context, 0x9999999999));
  EXPECT_EQ(nullptr, outer->GetCallSiteForReturnTo(symbol_context, 0));

  // Inside a code block but no matching call site.
  EXPECT_EQ(nullptr, outer->GetCallSiteForReturnTo(symbol_context, kLoadAddress + 0x1001));

  // Check all the call site addresses for matches.
  EXPECT_EQ(first_child_cs1.get(), outer->GetCallSiteForReturnTo(
                                       symbol_context, kLoadAddress + kFirstChildReturnAddress1));
  EXPECT_EQ(first_child_cs2.get(), outer->GetCallSiteForReturnTo(
                                       symbol_context, kLoadAddress + kFirstChildReturnAddress2));
  EXPECT_EQ(second_child_cs.get(), outer->GetCallSiteForReturnTo(
                                       symbol_context, kLoadAddress + kSecondChildReturnAddress));
}

}  // namespace zxdb
