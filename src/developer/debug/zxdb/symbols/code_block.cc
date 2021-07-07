// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/code_block.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/symbols/call_site.h"
#include "src/developer/debug/zxdb/symbols/call_site_parameter.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

CodeBlock::CodeBlock(DwarfTag tag) : Symbol(tag) {
  FX_DCHECK(tag == DwarfTag::kSubprogram || tag == DwarfTag::kInlinedSubroutine ||
            tag == DwarfTag::kLexicalBlock);
}

CodeBlock::~CodeBlock() = default;

fxl::RefPtr<CodeBlock> CodeBlock::GetContainingBlock() const {
  // Generic code blocks' containing block is just the parent. This is overridden by Function for
  // more specific behavior.
  auto owning_parent = parent().Get();
  return RefPtrTo(owning_parent->As<CodeBlock>());
}

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
  // Don't consider blocks with no addresses as covering anything. Consider them empty. The DWARF
  // spec says it will be empty when there is no corresponding machine code.
  //
  // Empty blocks can get generated, for example, in a abstract origin of an inlined function. Clang
  // declares the local variables inside a nesting structure identical to the inlined code, but the
  // abstract origin has no code associated with it. We don't want to consider these empty blocks
  // as containing all code of the function since using them will lose the context associated with
  // the inlined instance.

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
    const CodeBlock* inner_block = inner.Get()->As<CodeBlock>();
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

fxl::RefPtr<CallSite> CodeBlock::GetCallSiteForReturnTo(
    const SymbolContext& symbol_context, TargetPointer absolute_return_address) const {
  // Generally this will be called on a symbol from a Location so |this| could be an inline
  // function. Because of the difference between return addresses and where the call site
  // definitions are (see below), the call site may be on our parent. So always go to the physical
  // function the address is in.
  fxl::RefPtr<CodeBlock> containing = GetContainingFunction(kPhysicalOnly);
  if (!containing)
    containing = RefPtrTo(this);  // No function, call back on just using |this|.

  // We assume the call site will always be in the innermost code block containing the address.
  // This requirement isn't specified by DWARF but is true due to language semantics.
  //
  // We're looking up by return address which might be the instruction after the call for inlines
  // or lexical blocks that end in a function call. Therefore, look up the code block by the
  // previous address because the call sites will be assigned to blocks by their call address.
  const CodeBlock* inner_block =
      containing->GetMostSpecificChild(symbol_context, absolute_return_address - 1, true);
  if (!inner_block)
    return nullptr;

  TargetPointer relative_return_address =
      symbol_context.AbsoluteToRelative(absolute_return_address);

  for (const LazySymbol& lazy : inner_block->call_sites()) {
    const CallSite* call_site = lazy.Get()->As<CallSite>();
    if (!call_site)
      continue;

    if (call_site->return_pc() && *call_site->return_pc() == relative_return_address)
      return RefPtrTo(call_site);
  }
  return nullptr;
}

fxl::RefPtr<Function> CodeBlock::GetContainingFunction(SearchFunction search) const {
  // Need to hold references when walking up the symbol hierarchy.
  fxl::RefPtr<CodeBlock> cur_block = RefPtrTo(this);
  while (cur_block) {
    if (const Function* function = cur_block->As<Function>()) {
      if (function && (search == kInlineOrPhysical || !function->is_inline()))
        return RefPtrTo(function);
    }

    cur_block = cur_block->GetContainingBlock();
  }
  return fxl::RefPtr<Function>();
}

std::vector<fxl::RefPtr<Function>> CodeBlock::GetInlineChain() const {
  std::vector<fxl::RefPtr<Function>> result;

  // Need to hold references when walking up the symbol hierarchy.
  fxl::RefPtr<CodeBlock> cur_block = RefPtrTo(this);
  while (cur_block) {
    if (const Function* function = cur_block->As<Function>()) {
      result.push_back(RefPtrTo(function));

      if (function->is_inline()) {
        // Follow the inlined structure via containing_block() rather than the lexical structure of
        // the inlined function (e.g. its parent class).
        auto containing = function->containing_block().Get();
        cur_block = RefPtrTo(containing->As<CodeBlock>());
      } else {
        // Just added containing non-inline function so we're done.
        break;
      }
    } else {
      cur_block = cur_block->GetContainingBlock();
    }
  }
  return result;
}

std::vector<fxl::RefPtr<Function>> CodeBlock::GetAmbiguousInlineChain(
    const SymbolContext& symbol_context, TargetPointer absolute_address) const {
  std::vector<fxl::RefPtr<Function>> result;

  // For simplicity this gets the inline chain and then filters for ambiguous locations. This may
  // throw away some work which GetInlineChain() did.
  std::vector<fxl::RefPtr<Function>> inline_chain = GetInlineChain();
  for (size_t i = 0; i < inline_chain.size(); i++) {
    result.push_back(inline_chain[i]);
    if (!inline_chain[i]->is_inline() ||
        inline_chain[i]->GetFullRange(symbol_context).begin() != absolute_address) {
      // Non-ambiguous location, we're done.
      break;
    }
  }

  return result;
}

}  // namespace zxdb
