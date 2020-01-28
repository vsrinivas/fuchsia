// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VISIT_SCOPES_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VISIT_SCOPES_H_

#include "lib/fit/function.h"

namespace zxdb {

class CodeBlock;
class Collection;
class InheritancePath;
class Symbol;

// Return value for the callback for visiting the different scopes. The return for the whole
// function will be that of the last executed callback.
enum class VisitResult {
  kDone,     // Stops iterating and indicates success.
  kAbort,    // Stops iterating and indicates failure.
  kContinue  // Continues iterating if possible.
};

// Calls the callback for all code blocks, going backwards in the hierarchy. The starting block is
// called first. Stops iterating when it hits a function boundary.
//
// The visited blocks will normally not outlive this call. If the caller wants to get any
// symbol objects out of the visitation callback, it should take references to it.
VisitResult VisitLocalBlocks(const CodeBlock* starting,
                             fit::function<VisitResult(const CodeBlock*)> cb);

// Calls the callback for all classes in the inheritance hierarchy of the given collection. This
// works backwards, first calling the callback with the |starting| input, then a depth-first
// traversal of the inheritance tree.
//
// The callback takes the current collection being iterated, as well as the offset of that
// collection from the beginning of the starting collection.
//
// TODO(brettw) remove the version that takes an offset.
VisitResult VisitClassHierarchy(const Collection* starting,
                                fit::function<VisitResult(const Collection*, uint64_t offset)> cb);
VisitResult VisitClassHierarchy(const Collection* starting,
                                fit::function<VisitResult(const InheritancePath& path)> cb);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VISIT_SCOPES_H_
