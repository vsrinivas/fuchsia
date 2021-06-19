// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/visit_scopes.h"

#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/inheritance_path.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"

namespace zxdb {

namespace {

VisitResult DoVisitClassHierarchy(InheritancePath* path,
                                  fit::function<VisitResult(const InheritancePath& path)>& cb) {
  if (VisitResult result = cb(*path); result != VisitResult::kContinue)
    return result;

  // Iterate through base classes.
  for (const auto& lazy_from : path->path().back().collection->inherited_from()) {
    const InheritedFrom* inherited_from = lazy_from.Get()->As<InheritedFrom>();
    if (!inherited_from)
      continue;

    const Collection* from_coll = inherited_from->from().Get()->As<Collection>();
    if (!from_coll)
      continue;

    path->path().emplace_back(RefPtrTo(inherited_from), RefPtrTo(from_coll));
    if (VisitResult result = DoVisitClassHierarchy(path, cb); result != VisitResult::kContinue)
      return result;
    path->path().pop_back();
  }

  return VisitResult::kContinue;
}

// This takes the current byte offset of the input Collection from the original call for the
// callbacks when this is issued recursively. The remaining_iters variable is decremented and if
// it reaches 0, iteration will abort with a failure.
VisitResult DoVisitDataMembers(const Collection* collection, const VisitDataMembersCallback& cb,
                               uint32_t collection_byte_offset, int& remaining_iters) {
  return VisitClassHierarchy(collection, [collection_byte_offset, &remaining_iters,
                                          &cb](const InheritancePath& path) {
    if (remaining_iters == 0)
      return VisitResult::kAbort;
    remaining_iters--;

    std::optional<uint32_t> base_offset_or = path.BaseOffsetInDerived();
    if (!base_offset_or)
      return VisitResult::kContinue;  // Virtual inheritance, skip this one.

    // Check all data members of this step in the class hierarchy.
    for (const auto& member_symbol : path.base()->data_members()) {
      const DataMember* member = member_symbol.Get()->As<DataMember>();
      if (!member)
        return VisitResult::kAbort;

      // Get the type of the member to see if we need to recurse into a collection's members.
      //
      // Assume that all members will have concrete types because they will be needed to define the
      // layout of the input concrete type. The StripCVT() call will have decoded to the underlying
      // collection if it is one.
      const Type* member_type = member->type().Get()->As<Type>();
      if (!member_type)
        return VisitResult::kAbort;  // Expect all members to have a type.
      member_type = member_type->StripCVT();
      const Collection* collection_member = member_type->As<Collection>();

      // Issue the callback for this data member. The byte offset of this data member is sum of the
      // byte offset of the current collection in the original call (collection_byte_offset), the
      // current base class in the input collection, and the member offset in the current base
      // class.
      bool is_leaf = !collection_member;
      uint32_t member_offset = collection_byte_offset + *base_offset_or + member->member_location();
      VisitResult result = cb(is_leaf, member_offset, member);
      if (result != VisitResult::kContinue)
        return result;

      if (!collection_member)
        continue;  // This member it itself not a collection, no need to recurse.

      // Recursively visit the data member's members.
      result = DoVisitDataMembers(collection_member, cb, member_offset, remaining_iters);
      if (result != VisitResult::kContinue)
        return result;
    }

    return VisitResult::kContinue;
  });
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

    if (cur_block->As<Function>() || !cur_block->parent())
      break;  // Don't iterate above functions.
    auto parent_ref = cur_block->parent().Get();
    cur_block = RefPtrTo(parent_ref->As<CodeBlock>());
  }
  return VisitResult::kContinue;
}

VisitResult VisitClassHierarchy(const Collection* starting,
                                fit::function<VisitResult(const InheritancePath& path)> cb) {
  InheritancePath path(RefPtrTo(starting));
  return DoVisitClassHierarchy(&path, cb);
}

VisitResult VisitDataMembers(const Collection* collection, const VisitDataMembersCallback& cb,
                             int max_members) {
  int remaining_iters = max_members;
  return DoVisitDataMembers(collection, cb, 0, remaining_iters);
}

}  // namespace zxdb
