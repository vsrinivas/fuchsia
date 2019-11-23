// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CODE_BLOCK_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CODE_BLOCK_H_

#include <vector>

#include "src/developer/debug/zxdb/common/address_ranges.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

class SymbolContext;

// Base class for anything that has code: lexical blocks, inlined subroutines, and functions. A
// DWARF lexical block is represented as a CodeBlock rather than a derived type since it has no
// additional attributes.
class CodeBlock : public Symbol {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const CodeBlock* AsCodeBlock() const override;

  // The valid ranges of code for this block. In many cases there will be only one range (most
  // functions specify DW_AT_low_pc and DW_AT_high_pc), but some blocks, especially inlined
  // subroutines, may be at multiple discontiguous ranges in the code (DW_AT_ranges are specified).
  // In this case, the ranges will be in sorted order.
  //
  // Some lexical blocks won't have location information in them. These are often strictly to hold
  // groups of variables, each of which has their own range of validity.
  //
  // Function declarations will have no ranges associated with them. These aren't strictly "code
  // blocks" but many functions won't have a declaration/implementation split and there's so much
  // overlap it's more convenient to just have one type representing both.
  //
  // These ranges will be RELATIVE to the module. See GetAbsoluteCodeRanges() to get absolute
  // addresses.
  const AddressRanges& code_ranges() const { return code_ranges_; }
  void set_code_ranges(AddressRanges r) { code_ranges_ = std::move(r); }

  // Retrieves the code ranges for this block in absolute addresses for the process.
  AddressRanges GetAbsoluteCodeRanges(const SymbolContext& symbol_context) const;

  // Computes the full code range covering all sub-ranges. There can be multiple code ranges that
  // can be discontiguous so not everything in this range is guaranteed to be inside the code block.
  // Returns empty AddressRange if there are no code ranges.
  AddressRange GetFullRange(const SymbolContext& symbol_context) const;

  // The lexical blocks that are children of this one.
  const std::vector<LazySymbol>& inner_blocks() const { return inner_blocks_; }
  void set_inner_blocks(std::vector<LazySymbol> ib) { inner_blocks_ = std::move(ib); }

  // Variables contained within this block.
  const std::vector<LazySymbol>& variables() const { return variables_; }
  void set_variables(std::vector<LazySymbol> v) { variables_ = std::move(v); }

  // Returns true if the block's code ranges contain the given address. A block with no specified
  // range will always return true.
  bool ContainsAddress(const SymbolContext& symbol_context, uint64_t absolute_address) const;

  // Recursively searches all children of this block for the innermost block covering the given
  // address. Returns |this| if the current block is already the most specific, or nullptr if the
  // current block doesn't contain the address.
  //
  // Whether this function will go into inlined subroutines is controlled by recurse_into_inlines.
  // In many cases the Stack will handle expanding inlined subroutines and one would use this
  // function to find the most specific code block in the current virtual frame.
  const CodeBlock* GetMostSpecificChild(const SymbolContext& symbol_context,
                                        uint64_t absolute_address,
                                        bool recurse_into_inlines = false) const;

  // Recursively searches the containing blocks until it finds a function. If this code block is a
  // function, returns |this| as a Function. Returns null on error, but this should not happen for
  // well-formed symbols (all code should be inside functions).
  fxl::RefPtr<Function> GetContainingFunction() const;

  // Returns the chain of inline functions to the current code block.
  //
  // The returned vector will go back in time. The 0 item will be the most specific function
  // containing this code block (always GetContainingFunction(), will be = |this| if this is a
  // function).
  //
  // The back "should" be the containing non-inlined function (this depends on the symbols declaring
  // a function for the code block which they should do, but calling code shouldn't crash on
  // malformed symbols).
  //
  // If the current block is not in an inline function, the returned vector will have one element.
  std::vector<fxl::RefPtr<Function>> GetInlineChain() const;

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(CodeBlock);
  FRIEND_MAKE_REF_COUNTED(CodeBlock);

  explicit CodeBlock(DwarfTag tag);
  ~CodeBlock() override;

 private:
  AddressRanges code_ranges_;
  std::vector<LazySymbol> inner_blocks_;
  std::vector<LazySymbol> variables_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CODE_BLOCK_H_
