// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_MATCHER_H_
#define ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_MATCHER_H_

#include <lib/devicetree/devicetree.h>
#include <lib/devicetree/internal/matcher.h>
#include <lib/devicetree/matcher-result.h>
#include <lib/devicetree/path.h>

#include <algorithm>
#include <string_view>
#include <type_traits>

namespace devicetree {

// Scans |tree|, visiting each node at most |kMaxRescan| times. |kMaxRescan| is calculated
// from the MatcherResult<size_t N> and whether the matcher needs to resolve alias or not.
//
// A matcher that will not need to resolve aliases must declare the following interface:
//
// struct Matcher {
//   // Match will call |matcher(node, props)| on each node that the matcher manifests interest in.
//   //
//   // A matcher declares interest on a node |m|, when |matcher(path, props)| returns true on every
//   // node |n| that is an ancestor of |m|.
//   //
//   // |path| represent the path to the current node of the in progress walk of the tree.
//   // |props| are the list of properties contained in the current node.
//   MatcherResult<size_t> operator()(const NodePath& path, Properties properties);
// }
//
// A matcher must define one and only one of those interfaces.
//
// Match will return the number of scans performed if all matchers complete successfully.
// If not every matcher is done, then will return -1.
template <typename... Matchers>
size_t Match(Devicetree& tree, Matchers&&... matchers) {
  static_assert(sizeof...(Matchers) > 0);
  static_assert((std::is_invocable_v<Matchers, const NodePath&, Properties> && ...),
                "Matchers must implement the Matcher API.");
  return internal::Match(tree, matchers...);
}

}  // namespace devicetree

#endif  // ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_MATCHER_H_
