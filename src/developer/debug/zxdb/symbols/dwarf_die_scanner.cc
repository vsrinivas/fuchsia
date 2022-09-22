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
}

DwarfDieScanner2::~DwarfDieScanner2() = default;

const llvm::DWARFDebugInfoEntry* DwarfDieScanner2::Prepare() {
  if (done())
    return nullptr;

  cur_die_ = unit_->getDIEAtIndex(die_index_).getDebugInfoEntry();

#if defined(LLVM_USING_OLD_PREBUILT)
  uint32_t parent_idx = cur_die_->getParentIdx().getValueOr(kNoParent);
#else
  uint32_t parent_idx = cur_die_->getParentIdx().value_or(kNoParent);
#endif

  while (!tree_stack_.empty() && tree_stack_.back().index != parent_idx)
    tree_stack_.pop_back();
  tree_stack_.emplace_back(die_index_, false);

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

  return cur_die_;
}

void DwarfDieScanner2::Advance() {
  FX_DCHECK(!done());

  die_index_++;
}

uint32_t DwarfDieScanner2::GetParentIndex(uint32_t index) const {
#if defined(LLVM_USING_OLD_PREBUILT)
  return unit_->getDIEAtIndex(index).getDebugInfoEntry()->getParentIdx().getValueOr(kNoParent);
#else
  return unit_->getDIEAtIndex(index).getDebugInfoEntry()->getParentIdx().value_or(kNoParent);
#endif
}

}  // namespace zxdb
