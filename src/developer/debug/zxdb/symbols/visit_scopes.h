// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VISIT_SCOPES_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VISIT_SCOPES_H_

#include "lib/fit/function.h"

namespace zxdb {

class CodeBlock;
class Collection;
class DataMember;
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
// The callback gives the path from the input derived class to the current base class being
// iterated over.
//
// Watch out, the classes in the InheritancePath may not necessarily be concrete so call
// GetConcreteType() as necessary.
VisitResult VisitClassHierarchy(const Collection* starting,
                                fit::function<VisitResult(const InheritancePath& path)> cb);

// Calls the given callback for every data member of a collection. To avoid cases where the symbols
// are self-referential (this should be impossible but the symbols could be corrupted), iteration
// will stop after max_items (counts both data members and class hiararchy steps).
//
// The input collection must be concrete.
//
// The callback contains information across two dimensions (inheritance and nested members). Each
// inherited class can have members, and each member can have its own inheritance tree. So the
// path to get from the input Collection to the current data member is some arbitrary sequence of
// nested members and inheritance.
//
// Some members will be collections themselves and will therefore be iterated into. These
// member collections will have "is_leaf = false" to indicate that the data inside of them will be
// seen later (even if the collection is empty). For members like integers and pointers that have
// no other data inside them, "is_leaf" will be set to true.
//
// The net byte offset of each member within the input Collection is passed to the callback. This
// value should be used instead of DataMember::member_offset() because it takes into account all of
// these nested inheritance and nested member variables.
//
// This class doesn't support virtual inheritance because there is no simple offset in this case.
using VisitDataMembersCallback =
    fit::function<VisitResult(bool is_leaf, uint32_t net_byte_offset, const DataMember* member)>;
VisitResult VisitDataMembers(const Collection* collection, const VisitDataMembersCallback& cb,
                             int max_items = 4096);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_VISIT_SCOPES_H_
