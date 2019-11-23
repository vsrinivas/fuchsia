// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/code_block.h"

#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

CodeBlock::CodeBlock(DwarfTag tag) : Symbol(tag) {
  FXL_DCHECK(tag == DwarfTag::kSubprogram || tag == DwarfTag::kInlinedSubroutine ||
             tag == DwarfTag::kLexicalBlock);
}

CodeBlock::~CodeBlock() = default;

const CodeBlock* CodeBlock::AsCodeBlock() const { return this; }

AddressRanges CodeBlock::GetAbsoluteCodeRanges(const SymbolContext& symbol_context) const {
  return symbol_context.RelativeToAbsolute(code_ranges());
}

AddressRange CodeBlock::GetFullRange(const SymbolContext& symbol_context) const {
  if (code_ranges_.empty())
    return AddressRange();
  return AddressRange(symbol_context.RelativeToAbsolute(code_ranges_.front().begin()),
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

const CodeBlock* CodeBlock::GetMostSpecificChild(const SymbolContext& symbol_context,
                                                 uint64_t absolute_address,
                                                 bool recurse_into_inlines) const {
  if (!ContainsAddress(symbol_context, absolute_address))
    return nullptr;  // This block doesn't contain the address.

  for (const auto& inner : inner_blocks_) {
    // Don't expect more than one inner block to cover the address, so return
    // the first match. Everything in the inner_blocks_ should resolve to a
    // CodeBlock object.
    const CodeBlock* inner_block = inner.Get()->AsCodeBlock();
    if (!inner_block)
      continue;  // Corrupted symbols.
    if (!recurse_into_inlines && inner_block->tag() == DwarfTag::kInlinedSubroutine)
      continue;  // Skip inlined function.

    const CodeBlock* found =
        inner_block->GetMostSpecificChild(symbol_context, absolute_address, recurse_into_inlines);
    if (found)
      return found;
  }

  // This block covers the address but no children do.
  return this;
}

fxl::RefPtr<Function> CodeBlock::GetContainingFunction() const {
  // Need to hold references when walking up the symbol hierarchy.
  fxl::RefPtr<CodeBlock> cur_block = RefPtrTo(this);
  while (cur_block) {
    if (const Function* function = cur_block->AsFunction())
      return RefPtrTo(function);

    auto parent_ref = cur_block->parent().Get();
    cur_block = RefPtrTo(parent_ref->AsCodeBlock());
  }
  return fxl::RefPtr<Function>();
}

std::vector<fxl::RefPtr<Function>> CodeBlock::GetInlineChain() const {
  std::vector<fxl::RefPtr<Function>> result;

  // Need to hold references when walking up the symbol hierarchy.
  fxl::RefPtr<CodeBlock> cur_block = RefPtrTo(this);
  while (cur_block) {
    if (const Function* function = cur_block->AsFunction()) {
      result.push_back(RefPtrTo(function));

      if (function->is_inline()) {
        // Follow the inlined structure via containing_block() rather than the lexical structure of
        // the inlined function (e.g. its parent class).
        auto containing = function->containing_block().Get();
        cur_block = RefPtrTo(containing->AsCodeBlock());
      } else {
        // Just added containing non-inline function so we're done.
        break;
      }
    } else {
      auto parent_ref = cur_block->parent().Get();
      cur_block = RefPtrTo(parent_ref->AsCodeBlock());
    }
  }
  return result;
}

}  // namespace zxdb
