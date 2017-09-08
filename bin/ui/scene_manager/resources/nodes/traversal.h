// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/import.h"
#include "garnet/bin/ui/scene_manager/resources/nodes/node.h"

namespace scene_manager {

//
// Front-to-back traversals.
// Applies the functor to direct descendants in front-to-back order.
//
// This is the order in which they should be drawn to ensure that objects
// at the same elevation correctly obscure one another.
//
// This is also the order in which hit testing should be performed to ensure
// that the objects at the same elevation are evaluated from most to least
// specifically hit.
//
// The functor's signature must be |void(Node* node)|.
//

template <typename Callable>
void ForEachPartFrontToBack(const Node& node, const Callable& func) {
  // Process most recently added parts first.
  for (auto it = node.parts().rbegin(); it != node.parts().rend(); ++it) {
    func(it->get());
  }
}

template <typename Callable>
void ForEachChildFrontToBack(const Node& node, const Callable& func) {
  // Process most recently added children first.
  for (auto it = node.children().rbegin(); it != node.children().rend(); ++it) {
    func(it->get());
  }
}

template <typename Callable>
void ForEachImportFrontToBack(const Node& node, const Callable& func) {
  // Process most recently added imports first.
  for (auto it = node.imports().rbegin(); it != node.imports().rend(); ++it) {
    func(static_cast<Node*>((*it)->delegate()));
  }
}

template <typename Callable>
void ForEachChildAndImportFrontToBack(const Node& node, const Callable& func) {
  ForEachChildFrontToBack(node, func);
  ForEachImportFrontToBack(node, func);
}

template <typename Callable>
void ForEachDirectDescendantFrontToBack(const Node& node,
                                        const Callable& func) {
  ForEachChildAndImportFrontToBack(node, func);
  ForEachPartFrontToBack(node, func);
}

//
// Traversals with early termination once the functor returns true.
//
// The functor's signature must be |bool(const Node* node)|.
//

template <typename Callable>
bool ForEachPartFrontToBackUntilTrue(const Node& node, const Callable& func) {
  // Process most recently added parts first.
  for (auto it = node.parts().rbegin(); it != node.parts().rend(); ++it) {
    if (func(it->get()))
      return true;
  }
  return false;
}

template <typename Callable>
bool ForEachChildFrontToBackUntilTrue(const Node& node, const Callable& func) {
  // Process most recently added children first.
  for (auto it = node.children().rbegin(); it != node.children().rend(); ++it) {
    if (func(it->get()))
      return true;
  }
  return false;
}

template <typename Callable>
bool ForEachImportFrontToBackUntilTrue(const Node& node, const Callable& func) {
  // Process most recently added imports first.
  for (auto it = node.imports().rbegin(); it != node.imports().rend(); ++it) {
    if (func(static_cast<Node*>((*it)->delegate())))
      return true;
  }
  return false;
}

template <typename Callable>
bool ForEachChildAndImportFrontToBackUntilTrue(const Node& node,
                                               const Callable& func) {
  return ForEachChildFrontToBackUntilTrue(node, func) ||
         ForEachImportFrontToBackUntilTrue(node, func);
}

}  // namespace scene_manager
