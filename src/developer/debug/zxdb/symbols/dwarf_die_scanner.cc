// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_die_scanner.h"

#include <lib/syslog/cpp/macros.h>

#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "src/developer/debug/zxdb/symbols/dwarf_tag.h"

namespace zxdb {

DwarfDieScanner2::DwarfDieScanner2(llvm::DWARFUnit* unit) : unit_(unit) {
  die_count_ = unit_->getNumDIEs();

  // We prefer not to reallocate and normally the C++ component depth is < 8.
  tree_stack_.reserve(8);

#if defined(LLVM_USING_OLD_PREBUILT)
  parent_indices_.resize(die_count_);
  tree_stack_.emplace_back(-1, kNoParent, false);
#endif
}

DwarfDieScanner2::~DwarfDieScanner2() = default;

const llvm::DWARFDebugInfoEntry* DwarfDieScanner2::Prepare() {
  if (done())
    return nullptr;

  cur_die_ = unit_->getDIEAtIndex(die_index_).getDebugInfoEntry();

#if defined(LLVM_USING_OLD_PREBUILT)
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
    tree_stack_.emplace_back(current_depth, die_index_, false);
  }
#else
  uint32_t parent_idx = cur_die_->getParentIdx().getValueOr(kNoParent);

  while (!tree_stack_.empty() && tree_stack_.back().index != parent_idx)
    tree_stack_.pop_back();
  tree_stack_.emplace_back(die_index_, false);
#endif

  // Fix up the inside function flag.
  switch (static_cast<DwarfTag>(cur_die_->getTag())) {
    case DwarfTag::kLexicalBlock:
    case DwarfTag::kVariable:
      // Inherits from previous. For a block and a variable there should always be the parent,
      // since at least there's the unit root DIE.
      FX_DCHECK(tree_stack_.size() >= 2);
      tree_stack_.back().inside_function = tree_stack_[tree_stack_.size() - 2].inside_function;
      break;
    case DwarfTag::kSubprogram:
    case DwarfTag::kInlinedSubroutine:
      tree_stack_.back().inside_function = true;
      break;
    default:
      tree_stack_.back().inside_function = false;
      break;
  }

#if defined(LLVM_USING_OLD_PREBUILT)
  // Save parent info. The parent of this node is the one right before the
  // current one (the last one in the stack).
  parent_indices_[die_index_] = (tree_stack_.end() - 2)->index;
#endif

  return cur_die_;
}

void DwarfDieScanner2::Advance() {
  FX_DCHECK(!done());

  die_index_++;
}

#if !defined(LLVM_USING_OLD_PREBUILT)
uint32_t DwarfDieScanner2::GetParentIndex(uint32_t index) const {
  return unit_->getDIEAtIndex(index).getDebugInfoEntry()->getParentIdx().getValueOr(kNoParent);
}
#endif

}  // namespace zxdb
