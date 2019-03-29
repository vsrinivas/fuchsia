// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string_view>
#include <utility>

#include "src/developer/debug/zxdb/expr/identifier.h"

namespace zxdb {

class ModuleSymbolIndex;
class ModuleSymbolIndexNode;

class IndexWalker {
 public:
  // Starts from the root scope in the index. The pointer must outlive this
  // class.
  explicit IndexWalker(const ModuleSymbolIndex* index);

  ~IndexWalker();

  const ModuleSymbolIndexNode* current() const {
    return path_.empty() ? nullptr : path_.back();
  }

  // Goes up one level. If the current scope is "my_namespace::MyClass",
  // the new scope will be "my_namespace". Returns true if anything happened,
  // false if the current location is at the root already.
  bool WalkUp();

  // Moves to a child of the current component that's an exact match of the
  // given component name. Returns true if there was a match, false if not (in
  // which case the location has not changed).
  bool WalkInto(const Identifier::Component& comp);

  // Moves to a child of the current component that matches the given
  // identifier (following all components). Returns true if there was a match,
  // false if not (in which case the location has not changed).
  //
  // NOTE: this does not treat identifiers that start with "::" differently,
  // so will always attempt to do a relative name resolution. Handling which
  // scopes to search in is the job of the caller.
  bool WalkInto(const Identifier& ident);

  // Like WalkInto but does a best effort and always commits the results. This
  // is typically used to move to the starting point in an index for searching:
  // just because that exact namespace isn't in the index, doesn't mean one
  // can't resolve variables in it.
  //
  // If given "foo::Bar", and "foo" exists but has no "Bar inside of it,
  // this will walk to "foo" and returns false. If "Bar" did exist, it would
  // walk into it and return true.
  bool WalkIntoClosest(const Identifier& ident);

  // Returns true if the given component matches the given string from the
  // index. This will do limited canonicalization on the index string so a
  // comparison of template parameters is possible.
  static bool ComponentMatches(const std::string& index_string,
                               const Identifier::Component& comp);

  // Returns true if the component name matches the stuff in the index string
  // before any template parameters.
  static bool ComponentMatchesNameOnly(const std::string& index_string,
                                       const Identifier::Component& comp);

  // Returns true if the template parts of the component match a canonicalized
  // version of the template parameters extracted from the index string.
  static bool ComponentMatchesTemplateOnly(const std::string& index_string,
                                           const Identifier::Component& comp);

  // Returns true if all templates using the given base |name| will be before
  // the given indexed name in an index sorted by ASCII string values.
  static bool IsIndexStringBeyondName(std::string_view index_name,
                                      std::string_view name);

 private:
  // The path of index nodes to the current location. The current location is
  // the back() of this vector. We need to keep track of this to move up the
  // tree since ModuleSymbolIndexNodes don't track their parent pointer.
  std::vector<const ModuleSymbolIndexNode*> path_;
};

}  // namespace zxdb
