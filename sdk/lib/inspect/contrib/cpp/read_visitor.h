// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CONTRIB_CPP_READ_VISITOR_H_
#define LIB_INSPECT_CONTRIB_CPP_READ_VISITOR_H_

#include <lib/fit/function.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/stdcompat/string_view.h>

#include <stack>

namespace inspect {
namespace contrib {

// The wildcard character for visiting all children or all properties of a node.
constexpr char kPathWildcard[] = "*";

// The recursive wildcard character for visiting all properties of all children recursively.
constexpr char kPathRecursive[] = "**";

// Visit properties of a given type using a property selector.
//
// The property_selector defines the list of node names to follow, and it always ends in a
// property name. Special values inspect::kPathWildcard and inspect::kPathRecursive may be
// set to match all children/properties of a node or recurse to all child nodes respectively.
//
// Examples:
//   {"child", "value"}          - Match only root/child/value.
//   {"child", "*"}              - Match all properties of root/child
//   {"child", "*", "value"}     - Match all properties of children of root/child that are
//                                 called "value". Such as root/child/a/value and
//                                 root/child/b/value
//   {"child", "**"}             - Match all values under the subtree rooted at root/child.
//
// Note that this function may be called for a specific type only, it will need to be called
// multiple times to match multiple types.
//
// Examples:
//   IntPropertyValue
//   IntArrayValue
//   StringPropertyValue
//
// The visitor obtains the matching path (not including root) to each matching property as well
// as the decoded property itself.
//
// Returns true if all eligible properties were visited, and false if there was an error with the
// property selector.
template <typename T>
bool VisitProperties(
    const inspect::Hierarchy& hierarchy, const std::vector<cpp17::string_view>& property_selector,
    fit::function<void(const std::vector<cpp17::string_view>&, const T&)> visitor) {
  struct ctx {
    const Hierarchy* hierarchy;
    size_t path_index;
    std::vector<cpp17::string_view> path;
  };

  if (property_selector.empty()) {
    return false;
  }

  std::stack<ctx> ctx_stack;
  ctx_stack.push(ctx{.hierarchy = &hierarchy, .path_index = 0, .path = {}});
  while (!ctx_stack.empty()) {
    auto top = ctx_stack.top();
    ctx_stack.pop();

    const auto& cur_path = property_selector[top.path_index];
    bool is_end = top.path_index == property_selector.size() - 1;
    bool recurse = cur_path == kPathRecursive;

    if (!is_end && recurse) {
      // We do not support recursive matching in the middle of a path.
      return false;
    }

    if (is_end) {
      for (const auto& property : top.hierarchy->node().properties()) {
        if (cur_path != kPathWildcard && cur_path != kPathRecursive &&
            cur_path != property.name()) {
          continue;
        }
        if (property.template Contains<T>()) {
          top.path.push_back(property.name());
          visitor(top.path, property.template Get<T>());
          top.path.pop_back();
        }
      }
    }

    if (!is_end || recurse) {
      for (const auto& child : top.hierarchy->children()) {
        auto next_path = top.path;
        next_path.push_back(child.name());
        ctx_stack.push({.hierarchy = &child,
                        .path_index = top.path_index + ((recurse) ? 0 : 1),
                        .path = next_path});
      }
    }
  }

  return true;
}

}  // namespace contrib
}  // namespace inspect

#endif  // LIB_INSPECT_CONTRIB_CPP_READ_VISITOR_H_
