// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/visit_scopes.h"

#include "garnet/bin/zxdb/symbols/code_block.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/inherited_from.h"

namespace zxdb {

namespace {

bool DoVisitClassHierarchy(
    const Collection* current,
    uint32_t offset,
    std::function<bool(const Collection*, uint32_t offset)>& cb) {
  if (cb(current, offset))
    return true;

  // Iterate through base classes.
  for (const auto& lazy_from : current->inherited_from()) {
    const InheritedFrom* inherited_from = lazy_from.Get()->AsInheritedFrom();
    if (!inherited_from)
      continue;

    const Collection* from_coll = inherited_from->from().Get()->AsCollection();
    if (!from_coll)
      continue;

    if (DoVisitClassHierarchy(from_coll, offset + inherited_from->offset(), cb))
      return true;
  }

  return false;
}

}  // namespace

bool VisitClassHierarchy(
    const Collection* starting,
    std::function<bool(const Collection*, uint32_t offset)> cb) {
  return DoVisitClassHierarchy(starting, 0, cb);
}

}  // namespace zxdb
