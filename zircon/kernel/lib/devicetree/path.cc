// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/devicetree/path.h"

#include <lib/devicetree/devicetree.h>
#include <zircon/assert.h>

#include <string_view>

namespace devicetree {

fit::result<PathResolver::ResolveError, ResolvedPath> PathResolver::Resolve(
    std::string_view maybe_aliased_path) const {
  if (maybe_aliased_path.empty()) {
    return fit::ok(ResolvedPath{});
  }

  if (maybe_aliased_path[0] != '/') {
    if (!aliases_) {
      return fit::error(ResolveError::kNoAliases);
    }

    std::string_view alias = maybe_aliased_path.substr(0, maybe_aliased_path.find('/'));
    std::string_view suffix = maybe_aliased_path.substr(alias.size());
    if (!suffix.empty()) {
      suffix.remove_prefix(1);
    }

    for (auto [name, value] : *aliases_) {
      if (name != alias) {
        continue;
      }
      auto maybe_abs_path = value.AsString();
      if (!maybe_abs_path.has_value() || maybe_abs_path->empty()) {
        return fit::error(ResolveError::kBadAlias);
      }
      return fit::ok(ResolvedPath{.prefix = *maybe_abs_path, .suffix = suffix});
    }

    // We did not find a matching alias.
    return fit::error(ResolveError::kBadAlias);
  }

  return fit::ok(ResolvedPath{.prefix = maybe_aliased_path});
}

CompareResult ComparePath(const NodePath& path_a, const ResolvedPath& path_b) {
  // Compare stem.
  auto [prefix_a_it, prefix_b_it] = internal::CompareRangesOfNodes(
      path_a.begin(), path_a.end(), path_b.Prefix().begin(), path_b.Prefix().end());

  // They both point to the mismatching element.
  if (prefix_a_it != path_a.end() && prefix_b_it != path_b.Prefix().end()) {
    return kIsMismatch;
  }

  if (prefix_b_it != path_b.Prefix().end()) {
    return kIsAncestor;
  }

  auto [suffix_a_it, suffix_b_it] = internal::CompareRangesOfNodes(
      prefix_a_it, path_a.end(), path_b.Suffix().begin(), path_b.Suffix().end());

  // Exhausted the node path components but the stem still has more elements.
  if (suffix_a_it != path_a.end() && suffix_b_it != path_b.Suffix().end()) {
    return kIsMismatch;
  }

  if (suffix_a_it == path_a.end() && suffix_b_it != path_b.Suffix().end()) {
    return kIsAncestor;
  }

  if (suffix_a_it != path_a.end() && suffix_b_it == path_b.Suffix().end()) {
    return kIsDescendant;
  }

  return kIsMatch;
}

CompareResult ComparePath(const NodePath& path_a, std::string_view absolute_path_b) {
  ZX_ASSERT(absolute_path_b.empty() || absolute_path_b[0] == '/');
  return ComparePath(path_a, {.prefix = absolute_path_b});
}

}  // namespace devicetree
