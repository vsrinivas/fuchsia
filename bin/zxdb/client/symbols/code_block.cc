// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/code_block.h"

#include "lib/fxl/logging.h"

namespace zxdb {

CodeBlock::CodeBlock(int tag) : Symbol(tag) {}
CodeBlock::~CodeBlock() = default;

const CodeBlock* CodeBlock::AsCodeBlock() const { return this; }

bool CodeBlock::ContainsAddress(uint64_t address) const {
  if (code_ranges_.empty())
    return true;  // No defined code range, assume always valid.

  for (const auto& range : code_ranges_) {
    if (address >= range.first && address < range.second)
      return true;
  }
  return false;
}

const CodeBlock* CodeBlock::GetMostSpecificChild(uint64_t address) const {
  if (!ContainsAddress(address))
    return nullptr;  // This block doesn't contain the address.

  for (const auto& inner : inner_blocks_) {
    // Don't expect more than one inner block to cover the address, so return
    // the first match. Everything in the inner_blocks_ should resolve to a
    // CodeBlock object.
    const CodeBlock* inner_block = inner.Get()->AsCodeBlock();
    if (!inner_block)
      continue;  // Corrupted symbols.
    const CodeBlock* found = inner_block->GetMostSpecificChild(address);
    if (found)
      return found;
  }

  // This block covers the address but no children do.
  return this;
}

}  // namespace zxdb
