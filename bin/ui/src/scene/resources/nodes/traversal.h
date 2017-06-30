// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/resources/import.h"
#include "apps/mozart/src/scene/resources/nodes/node.h"

namespace mozart {
namespace scene {

// Applies a functor to all descendents of the given node.
// The functor's signature must be |void(const Node* node)|.
template <typename Callable>
void ForEachDirectDescentant(const Node& node, Callable func) {
  for (auto& part : node.parts()) {
    func(part.get());
  }
  for (auto& child : node.children()) {
    func(child.get());
  }
  for (auto& import : node.imports()) {
    func(static_cast<Node*>(import->delegate()));
  }
}

}  // namespace scene
}  // namespace mozart
