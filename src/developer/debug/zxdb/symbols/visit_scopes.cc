// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/visit_scopes.h"

#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"

namespace zxdb {

namespace {

VisitResult DoVisitClassHierarchy(
    const Collection* current, uint64_t offset,
    fit::function<VisitResult(const Collection*, uint64_t offset)>& cb) {
  VisitResult result = cb(current, offset);
  if (result != VisitResult::kContinue)
    return result;

  // Iterate through base classes.
  for (const auto& lazy_from : current->inherited_from()) {
    const InheritedFrom* inherited_from = lazy_from.Get()->AsInheritedFrom();
    if (!inherited_from)
      continue;

    const Collection* from_coll = inherited_from->from().Get()->AsCollection();
    if (!from_coll)
      continue;

    result = DoVisitClassHierarchy(from_coll, offset + inherited_from->offset(), cb);
    if (result != VisitResult::kContinue)
      return result;
  }

  return VisitResult::kContinue;
}

}  // namespace

VisitResult VisitLocalBlocks(const CodeBlock* starting,
                             fit::function<VisitResult(const CodeBlock*)> cb) {
  // Need to hold references when walking up the symbol hierarchy.
  fxl::RefPtr<CodeBlock> cur_block = RefPtrTo(starting);
  while (cur_block) {
    VisitResult result = cb(cur_block.get());
    if (result != VisitResult::kContinue)
      return result;

    if (cur_block->AsFunction() || !cur_block->parent())
      break;  // Don't iterate above functions.
    auto parent_ref = cur_block->parent().Get();
    cur_block = RefPtrTo(parent_ref->AsCodeBlock());
  }
  return VisitResult::kContinue;
}

VisitResult VisitClassHierarchy(const Collection* starting,
                                fit::function<VisitResult(const Collection*, uint64_t offset)> cb) {
  return DoVisitClassHierarchy(starting, 0, cb);
}

}  // namespace zxdb
