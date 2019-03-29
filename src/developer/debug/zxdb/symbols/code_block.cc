// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/code_block.h"

#include "src/lib/fxl/logging.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

CodeBlock::CodeBlock(DwarfTag tag) : Symbol(tag) {
  FXL_DCHECK(tag == DwarfTag::kSubprogram ||
             tag == DwarfTag::kInlinedSubroutine ||
             tag == DwarfTag::kLexicalBlock);
}

CodeBlock::~CodeBlock() = default;

const CodeBlock* CodeBlock::AsCodeBlock() const { return this; }

AddressRanges CodeBlock::GetAbsoluteCodeRanges(
    const SymbolContext& symbol_context) const {
  return symbol_context.RelativeToAbsolute(code_ranges());
}

AddressRange CodeBlock::GetFullRange(
    const SymbolContext& symbol_context) const {
  if (code_ranges_.empty())
    return AddressRange();
  return AddressRange(
      symbol_context.RelativeToAbsolute(code_ranges_.front().begin()),
      symbol_context.RelativeToAbsolute(code_ranges_.back().end()));
}

bool CodeBlock::ContainsAddress(const SymbolContext& symbol_context,
                                uint64_t absolute_address) const {
  if (code_ranges_.empty())
    return true;  // No defined code range, assume always valid.

  for (const auto& range : code_ranges_) {
    if (absolute_address >= symbol_context.RelativeToAbsolute(range.begin()) &&
        absolute_address < symbol_context.RelativeToAbsolute(range.end()))
      return true;
  }
  return false;
}

const CodeBlock* CodeBlock::GetMostSpecificChild(
    const SymbolContext& symbol_context, uint64_t absolute_address) const {
  if (!ContainsAddress(symbol_context, absolute_address))
    return nullptr;  // This block doesn't contain the address.

  for (const auto& inner : inner_blocks_) {
    // Don't expect more than one inner block to cover the address, so return
    // the first match. Everything in the inner_blocks_ should resolve to a
    // CodeBlock object.
    const CodeBlock* inner_block = inner.Get()->AsCodeBlock();
    if (!inner_block)
      continue;  // Corrupted symbols.
    const CodeBlock* found =
        inner_block->GetMostSpecificChild(symbol_context, absolute_address);
    if (found)
      return found;
  }

  // This block covers the address but no children do.
  return this;
}

const Function* CodeBlock::GetContainingFunction() const {
  const CodeBlock* cur_block = this;
  while (cur_block) {
    if (const Function* function = cur_block->AsFunction())
      return function;
    cur_block = cur_block->parent().Get()->AsCodeBlock();
  }
  return nullptr;
}

std::vector<const Function*> CodeBlock::GetInlineChain() const {
  std::vector<const Function*> result;

  const CodeBlock* cur_block = this;
  while (cur_block) {
    if (const Function* function = cur_block->AsFunction()) {
      result.push_back(function);

      if (function->is_inline()) {
        // Follow the inlined structure via containing_block() rather than
        // the lexical structure of the inlined function (e.g. its parent
        // class).
        cur_block = function->containing_block().Get()->AsCodeBlock();
      } else {
        // Just added containing non-inline function so we're done.
        break;
      }
    } else {
      cur_block = cur_block->parent().Get()->AsCodeBlock();
    }
  }
  return result;
}

}  // namespace zxdb
