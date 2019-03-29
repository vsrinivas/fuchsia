// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/index_walker.h"

#include <ctype.h>
#include <string.h>

#include <string_view>

#include "src/developer/debug/zxdb/common/err.h"
//#include "src/developer/debug/zxdb/common/string_util.h"
#include "garnet/bin/zxdb/symbols/module_symbol_index.h"
#include "garnet/bin/zxdb/symbols/module_symbol_index_node.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

// We don't expect to have identifiers with whitespace in them. If somebody
// does "Foo < Bar>" stop considering the name at the space.
inline bool IsNameEnd(char ch) { return isspace(ch) || ch == '<'; }

}  // namespace

IndexWalker::IndexWalker(const ModuleSymbolIndex* index) {
  path_.push_back(&index->root());
}

IndexWalker::~IndexWalker() = default;

bool IndexWalker::WalkUp() {
  if (path_.size() > 1) {
    // Don't walk above the root.
    path_.resize(path_.size() - 1);
    return true;
  }
  return false;
}

bool IndexWalker::WalkInto(const Identifier::Component& comp) {
  const ModuleSymbolIndexNode* node = path_.back();

  const std::string& comp_name = comp.name().value();

  // This is complicated by templates which can't be string-compared for
  // equality without canonicalization. Search everything in the index with the
  // same base (non-template-part) name. With the index being sorted, we can
  // start at the item that begins lexicographically >= the input.
  auto iter = node->sub().lower_bound(comp_name);
  if (iter == node->sub().end())
    return false;  // Nothing can match.

  if (!comp.has_template()) {
    // In the common case there is no template in the input, so we can just
    // check for exact string equality and be done.
    if (iter->first == comp_name) {
      path_.push_back(&iter->second);
      return true;
    }
    return false;
  }

  // Check all nodes until template canonicalization can't affect the
  // comparison.
  while (iter != node->sub().end() &&
         !IsIndexStringBeyondName(iter->first, comp_name)) {
    if (ComponentMatches(iter->first, comp)) {
      // Found match.
      path_.push_back(&iter->second);
      return true;
    }

    ++iter;
  }

  return false;
}

bool IndexWalker::WalkInto(const Identifier& ident) {
  IndexWalker sub(*this);
  if (!sub.WalkIntoClosest(ident))
    return false;

  // Full walk succeeded, commit.
  std::swap(path_, sub.path_);
  return true;
}

bool IndexWalker::WalkIntoClosest(const Identifier& ident) {
  for (const auto& comp : ident.components()) {
    if (!WalkInto(comp))
      return false;  // This component not found.
  }
  return true;
}

// static
bool IndexWalker::ComponentMatches(const std::string& index_string,
                                   const Identifier::Component& comp) {
  if (!ComponentMatchesNameOnly(index_string, comp))
    return false;
  // Only bother with the expensive template comparison on demand.
  return ComponentMatchesTemplateOnly(index_string, comp);
}

// static
bool IndexWalker::ComponentMatchesNameOnly(const std::string& index_string,
                                           const Identifier::Component& comp) {
  const std::string& comp_name = comp.name().value();
  if (comp_name.size() > index_string.size())
    return false;  // Index string can't contain the name.

  if (strncmp(comp_name.c_str(), index_string.c_str(), comp_name.size()) != 0)
    return false;  // Name prefix doesn't match.

  // The index string should be at the end or should have a template spec
  // following the name.
  return index_string.size() == comp_name.size() ||
         IsNameEnd(index_string[comp_name.size()]);
}

// static
bool IndexWalker::ComponentMatchesTemplateOnly(
    const std::string& index_string, const Identifier::Component& comp) {
  auto [err, index_ident] = Identifier::FromString(index_string);
  if (err.has_error())
    return false;

  // Each namespaced component should be a different layer of the index so
  // it should produce a one-component identifier. But this depends how the
  // symbols are structured which we don't want to make assumptions about.
  if (index_ident.components().size() != 1)
    return false;
  const auto& index_comp = index_ident.components()[0];

  if (comp.has_template() != index_comp.has_template())
    return false;
  return comp.template_contents() == index_comp.template_contents();
}

// static
bool IndexWalker::IsIndexStringBeyondName(std::string_view index_name,
                                          std::string_view name) {
  if (index_name.size() <= name.size()) {
    // The |index_name| is too small to start with the name and have template
    // stuff on it (which requires special handling), so we can directly return
    // the answer by string comparison.
    return index_name > name;
  }

  // When the first name.size() characters of the index string aren't the
  // same as the name, we don't need to worry about templates or anything
  // and can just return that comparison.
  std::string_view index_prefix = index_name.substr(0, name.size());
  int prefix_compare = index_prefix.compare(name);
  if (prefix_compare != 0)
    return prefix_compare > 0;  // Index is beyond the name by prefix only.

  // |index_name| starts with |name|. For the index node to be after all
  // possible templates of |name|, compare against the template begin
  // character. This does make the assumption that the compiler won't
  // write templates with a space after the name ("vector < int >").
  return index_name[name.size()] > '<';
}

}  // namespace zxdb
