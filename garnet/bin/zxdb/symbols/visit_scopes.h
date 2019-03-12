// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

namespace zxdb {

class Collection;
class Symbol;

enum class VisitResult {
  kDone,     // Stops iterating and indicates success.
  kAbort,    // Stops iterating and indicates failure.
  kNotFound  // Search failed at this level, continues iterating if possible.
};

// Calls the callback for all classes in the inheritance hierarchy of the given
// collection. This works backwards, first calling the callback with the
// |starting| input, then a depth-first traversal of the inheritance tree.
//
// The callback takes the current collection being iterated, as well as the
// offset of that collection from the beginning of the starting collection.
//
// The callback returns a completion flag. It should return false to keep
// iterating, or true to stop iteration. The return value of
// VisitClassHierarchy() will be that of the last executed callback.
VisitResult VisitClassHierarchy(
    const Collection* starting,
    std::function<VisitResult(const Collection*, uint32_t offset)> cb);

}  // namespace zxdb
