// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

namespace zxdb {

class CodeBlock;
class Collection;
class Symbol;

// Return value for the callback for visiting the different scopes. The return
// for the whole function will be that of the last executed callback.
enum class VisitResult {
  kDone,     // Stops iterating and indicates success.
  kAbort,    // Stops iterating and indicates failure.
  kContinue  // Continues iterating if possible.
};

// Calls the callback for all code blocks, going backwards in the hierarchy.
// The starting block is called first. Stops iterating when it hits a function
// boundary.
VisitResult VisitLocalBlocks(const CodeBlock* starting,
                             std::function<VisitResult(const CodeBlock*)> cb);

// Calls the callback for all classes in the inheritance hierarchy of the given
// collection. This works backwards, first calling the callback with the
// |starting| input, then a depth-first traversal of the inheritance tree.
//
// The callback takes the current collection being iterated, as well as the
// offset of that collection from the beginning of the starting collection.
VisitResult VisitClassHierarchy(
    const Collection* starting,
    std::function<VisitResult(const Collection*, uint64_t offset)> cb);

}  // namespace zxdb
