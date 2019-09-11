// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_die_scanner.h"

#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "src/developer/debug/zxdb/symbols/dwarf_tag.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

DwarfDieScanner::DwarfDieScanner(llvm::DWARFUnit* unit) : unit_(unit) {
  die_count_ = unit_->getNumDIEs();
  parent_indices_.resize(die_count_);
  tree_stack_.reserve(8);

  tree_stack_.emplace_back(-1, kNoParent, false);
}

DwarfDieScanner::~DwarfDieScanner() = default;

const llvm::DWARFDebugInfoEntry* DwarfDieScanner::Prepare() {
  if (done())
    return nullptr;

  cur_die_ = unit_->getDIEAtIndex(die_index_).getDebugInfoEntry();
  return cur_die_;
}

void DwarfDieScanner::Advance() {
  FXL_DCHECK(!done());

  // LLVM can provide the depth in O(1) time but not the parent.
  int current_depth = static_cast<int>(cur_die_->getDepth());
  if (current_depth == tree_stack_.back().depth) {
    // Common case: depth not changing. Just update the topmost item in the stack to point to the
    // current node.
    tree_stack_.back().index = die_index_;
  } else {
    // Tree changed. First check for moving up in the tree and pop the stack until we're at the
    // parent of the current level (this will do nothing when going deeper in the tree), then add
    // the current level.
    while (tree_stack_.back().depth >= current_depth)
      tree_stack_.pop_back();

    bool inside_function;
    switch (static_cast<DwarfTag>(cur_die_->getTag())) {
      case DwarfTag::kLexicalBlock:
        // Inherits from previous.
        inside_function = tree_stack_.back().inside_function;
        break;
      case DwarfTag::kSubprogram:
      case DwarfTag::kInlinedSubroutine:
        inside_function = true;
        break;
      default:
        inside_function = false;
        break;
    }

    tree_stack_.emplace_back(current_depth, die_index_, inside_function);
  }

  // Save parent info. The parent of this node is the one right before the
  // current one (the last one in the stack).
  parent_indices_[die_index_] = (tree_stack_.end() - 2)->index;

  die_index_++;
}

}  // namespace zxdb
