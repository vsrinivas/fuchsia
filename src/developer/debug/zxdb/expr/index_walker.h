// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_INDEX_WALKER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_INDEX_WALKER_H_

#include <string_view>
#include <utility>

#include "src/developer/debug/zxdb/expr/parsed_identifier.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/index_node.h"

namespace zxdb {

class Index;

// Provides an interface to walk the symbol index by component. This is used to iterate over the
// the current namespace looking for matches.
//
// This search is extra complicated because each index node is separated by child type (namespaces,
// functions, etc.). This means there can be more than one path to a given identifier depending on
// the types of things.
//
// This class provides an interface that walks into all such matches. This means that at any
// given level there can be multiple "current" nodes (this list is called a "Stage"). The "path_"
// is the hierarchy of these stages.
//
// It's possible for this graph to explode but the branching factor is currently only 4, and in
// practice it will almost always be 1 and will rarely be 2 (there are two different classes of
// symbol with the same name). As a result, an explosion of items to iterate over is unlikely.
class IndexWalker {
 public:
  using Stage = std::vector<const IndexNode*>;

  // Starts from the root scope in the index. The pointer must outlive this class.
  explicit IndexWalker(const Index* index);

  ~IndexWalker();

  // There should always be a "current" item which is at least the root of the index.
  const Stage& current() const { return path_.back(); }

  // Goes up one level. If the current scope is "my_namespace::MyClass", the new scope will be
  // "my_namespace". Returns true if anything happened. Returns false if the current location is at
  // the root already.
  bool WalkUp();

  // Moves to the children of the current component that's an exact match of the given component
  // name. Returns true if there was a match, false if not (in which case the location has not
  // changed).
  //
  // This ignores the separator, so if the input component is "::foo" this won't be treated as
  // the global name "foo" and go back to the root as C++ would, but will instead go into "foo" from
  // the current location. This is because this function will be called for each sub-component of an
  // identifier, and all non-toplevel components will have separators.
  bool WalkInto(const ParsedIdentifierComponent& comp);

  // Moves to the children of the current component that matches the given identifier (following all
  // components). Returns true if there was a match, false if not (in which case the location has
  // not changed).
  //
  // NOTE: this does not treat identifiers that start with "::" differently, so will always attempt
  // to do a relative name resolution. Handling which scopes to search in is the job of the caller.
  bool WalkInto(const ParsedIdentifier& ident);

  // Walks into a specific node. This node should be a child of one of the current() nodes. This
  // is used when code identifies a specific child rather than a general name it wants to walk
  // into.
  void WalkIntoSpecific(const IndexNode* node);

  // Like WalkInto but does a best effort and always commits the results. This is typically used to
  // move to the starting point in an index for searching: just because that exact namespace isn't
  // in the index, doesn't mean one can't resolve variables in it.
  //
  // If given "foo::Bar", and "foo" exists but has no "Bar inside of it, this will walk to "foo" and
  // returns false. If "Bar" did exist, it would walk into it and return true.
  bool WalkIntoClosest(const ParsedIdentifier& ident);

  // Returns true if the given component matches the given string from the index. This will do
  // limited canonicalization on the index string so a comparison of template parameters is
  // possible.
  static bool ComponentMatches(const std::string& index_string,
                               const ParsedIdentifierComponent& comp);

  // Returns true if the component name matches the stuff in the index string before any template
  // parameters.
  static bool ComponentMatchesNameOnly(const std::string& index_string,
                                       const ParsedIdentifierComponent& comp);

  // Returns true if the template parts of the component match a canonicalized version of the
  // template parameters extracted from the index string.
  static bool ComponentMatchesTemplateOnly(const std::string& index_string,
                                           const ParsedIdentifierComponent& comp);

  // Returns true if all templates using the given base |name| will be before the given indexed name
  // in an index sorted by ASCII string values.
  static bool IsIndexStringBeyondName(std::string_view index_name, std::string_view name);

 private:
  std::vector<Stage> path_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_INDEX_WALKER_H_
