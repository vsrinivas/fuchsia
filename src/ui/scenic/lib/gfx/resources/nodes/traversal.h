// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_TRAVERSAL_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_TRAVERSAL_H_

#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"

namespace scenic_impl {
namespace gfx {

//
// Front-to-back traversals.
// Applies the functor to direct descendants in front-to-back order.
//
// This is the order in which they should be drawn to ensure that objects
// at the same elevation correctly obscure one another.
//
// The functor's signature must be |void(Node* node)|.
//
template <typename Callable>
void ForEachChildFrontToBack(const Node& node, const Callable& func) {
  // Process most recently added children first.
  for (auto it = node.children().rbegin(); it != node.children().rend(); ++it) {
    func(it->get());
  }
}

//
// Traversals with early termination once the functor returns true.
//
// The functor's signature must be |bool(const Node* node)|.
//
template <typename Callable>
bool ForEachChildFrontToBackUntilTrue(const Node& node, const Callable& func) {
  // Process most recently added children first.
  for (auto it = node.children().rbegin(); it != node.children().rend(); ++it) {
    if (func(it->get()))
      return true;
  }
  return false;
}

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_TRAVERSAL_H_
