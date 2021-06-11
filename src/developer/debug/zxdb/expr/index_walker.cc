// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/index_walker.h"

#include <ctype.h>
#include <lib/syslog/cpp/macros.h>
#include <string.h>

#include <string_view>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/symbols/index.h"
#include "src/developer/debug/zxdb/symbols/index_node.h"

namespace zxdb {

namespace {

// We don't expect to have identifiers with whitespace in them. If somebody does "Foo < Bar>" stop
// considering the name at the space.
inline bool IsNameEnd(char ch) { return isspace(ch) || ch == '<'; }

// Finds all anonymous children of the nodes in the given stage and appends them recursively until
// there are no more to add.
//
// In the future we may want an option to trigger whether this function is called or not.
void AddAnonymousChildrenToStage(IndexWalker::Stage* stage) {
  // This implements a breadth-first search, adding all unnamed items.
  const std::string kEmpty;

  size_t last_pass_begin = 0;
  while (last_pass_begin < stage->size()) {
    size_t last_pass_end = stage->size();
    for (size_t i = last_pass_begin; i < last_pass_end; i++) {
      const IndexNode* node = (*stage)[i];

      // Add unnamed items. The common case is anonymous namespaces but we might also have unnamed
      // types for anonymous enums and structs.
      if (auto found = node->namespaces().find(kEmpty); found != node->namespaces().end())
        stage->push_back(&found->second);
      if (auto found = node->types().find(kEmpty); found != node->types().end())
        stage->push_back(&found->second);
    }
    last_pass_begin = last_pass_end;
  }
}

}  // namespace

IndexWalker::IndexWalker(const Index* index) {
  // Prefer not to reallocate the vector-of-vectors. It is rare for C++ namespace hierarchies to
  // be more than a couple of components long, so this number should cover most cases.
  path_.reserve(8);

  path_.push_back({&index->root()});
  AddAnonymousChildrenToStage(path_.data());
}

IndexWalker::~IndexWalker() = default;

bool IndexWalker::WalkUp() {
  if (path_.size() > 1) {
    // Don't walk above the root.
    path_.pop_back();
    return true;
  }
  return false;
}

// TODO(bug 6410) When we encounter an "inline" namespace, implicitly walk into it here, or have
// that controllable as an option. Inline namespaces produce a namespace with an implicit "using"
// statement.
bool IndexWalker::WalkInto(const ParsedIdentifierComponent& comp) {
  const Stage& old_stage = path_.back();

  const std::string& comp_name = comp.name();
  if (comp_name.empty())
    return true;  // No-op.

  Stage new_stage;
  for (const auto* old_node : old_stage) {
    for (int i = 0; i < static_cast<int>(IndexNode::Kind::kEndPhysical); i++) {
      const IndexNode::Map& map = old_node->MapForKind(static_cast<IndexNode::Kind>(i));

      // This is complicated by templates which can't be string-compared for equality without
      // canonicalization. Search everything in the index with the same base (non-template-part)
      // name. With the index being sorted, we can start at the item that begins lexicographically
      // >= the input.
      auto iter = map.lower_bound(comp_name);
      if (iter == map.end())
        continue;  // Nothing can match of this kind.

      if (!comp.has_template()) {
        // In the common case there is no template in the input, so we can just check for exact
        // string equality and be done with this kind.
        if (iter->first == comp_name)
          new_stage.push_back(&iter->second);
        continue;
      }

      // Check all nodes until template canonicalization can't affect the comparison.
      while (iter != map.end() && !IsIndexStringBeyondName(iter->first, comp_name)) {
        if (ComponentMatches(iter->first, comp)) {
          // Found match.
          new_stage.push_back(&iter->second);
          break;
        }

        ++iter;
      }
    }
  }

  if (new_stage.empty())
    return false;  // No children found.

  AddAnonymousChildrenToStage(&new_stage);

  // Commit the new found stuff.
  path_.push_back(std::move(new_stage));
  return true;
}

bool IndexWalker::WalkInto(const ParsedIdentifier& ident) {
  IndexWalker sub(*this);
  if (!sub.WalkIntoClosest(ident))
    return false;

  // Full walk succeeded, commit.
  std::swap(path_, sub.path_);
  return true;
}

void IndexWalker::WalkIntoSpecific(const IndexNode* node) { path_.push_back(Stage{node}); }

bool IndexWalker::WalkIntoClosest(const ParsedIdentifier& ident) {
  if (ident.qualification() == IdentifierQualification::kGlobal)
    path_.resize(1);  // Only keep the root.

  for (const auto& comp : ident.components()) {
    if (!WalkInto(comp))
      return false;  // This component not found.
  }
  return true;
}

// static
bool IndexWalker::ComponentMatches(const std::string& index_string,
                                   const ParsedIdentifierComponent& comp) {
  if (!ComponentMatchesNameOnly(index_string, comp))
    return false;
  // Only bother with the expensive template comparison on demand.
  return ComponentMatchesTemplateOnly(index_string, comp);
}

// static
bool IndexWalker::ComponentMatchesNameOnly(const std::string& index_string,
                                           const ParsedIdentifierComponent& comp) {
  const std::string& comp_name = comp.name();
  if (comp_name.size() > index_string.size())
    return false;  // Index string can't contain the name.

  if (strncmp(comp_name.c_str(), index_string.c_str(), comp_name.size()) != 0)
    return false;  // Name prefix doesn't match.

  // The index string should be at the end or should have a template spec
  // following the name.
  return index_string.size() == comp_name.size() || IsNameEnd(index_string[comp_name.size()]);
}

// static
bool IndexWalker::ComponentMatchesTemplateOnly(const std::string& index_string,
                                               const ParsedIdentifierComponent& comp) {
  ParsedIdentifier index_ident;
  Err err = ExprParser::ParseIdentifier(index_string, &index_ident);
  if (err.has_error())
    return false;

  // Each namespaced component should be a different layer of the index so it should produce a
  // one-component identifier. But this depends how the symbols are structured which we don't want
  // to make assumptions about.
  if (index_ident.components().size() != 1)
    return false;
  const auto& index_comp = index_ident.components()[0];

  if (comp.has_template() != index_comp.has_template())
    return false;
  return comp.template_contents() == index_comp.template_contents();
}

// static
bool IndexWalker::IsIndexStringBeyondName(std::string_view index_name, std::string_view name) {
  if (index_name.size() <= name.size()) {
    // The |index_name| is too small to start with the name and have template stuff on it (which
    // requires special handling), so we can directly return the answer by string comparison.
    return index_name > name;
  }

  // When the first name.size() characters of the index string aren't the same as the name, we don't
  // need to worry about templates or anything and can just return that comparison.
  std::string_view index_prefix = index_name.substr(0, name.size());
  int prefix_compare = index_prefix.compare(name);
  if (prefix_compare != 0)
    return prefix_compare > 0;  // Index is beyond the name by prefix only.

  // |index_name| starts with |name|. For the index node to be after all possible templates of
  // |name|, compare against the template begin character. This does make the assumption that the
  // compiler won't write templates with a space after the name ("vector < int >").
  return index_name[name.size()] > '<';
}

}  // namespace zxdb
