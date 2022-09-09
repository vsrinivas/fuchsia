// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/reachability.h"

#include <lib/syslog/cpp/macros.h>

#include <unordered_set>
#include <vector>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

namespace {
// Pushes `n` onto `stack` if `n` has not yet been visited. Returns true iff `n == goal`.
bool PushNode(const std::unordered_set<const Node*>& visited, std::vector<const Node*>& stack,
              const Node& n, const Node& goal) {
  if (&n == &goal) {
    return true;
  }
  if (visited.count(&n) == 0) {
    stack.push_back(&n);
  }
  return false;
}

// Returns `n.parent()` if `n` is a child source node of a meta node.
// Otherwise returns nullptr.
const Node* ParentOfChildSourceNode(const Node& n) {
  if (const auto parent = n.parent(); parent) {
    auto& sources = parent->child_sources();
    if (std::find_if(sources.begin(), sources.end(), [&n](auto x) { return &n == x.get(); }) !=
        sources.end()) {
      return parent.get();
    }
  }
  return nullptr;
}
}  // namespace

bool ExistsPath(const Node& src, const Node& dest) {
  std::unordered_set<const Node*> visited;
  std::vector<const Node*> stack;

  const Node* n = &src;
  for (;;) {
    visited.insert(n);

    // Push each outgoing edge.
    if (n->is_meta()) {
      // Meta -> Child Dests
      for (auto& child : n->child_dests()) {
        if (PushNode(visited, stack, *child, dest)) {
          return true;
        }
      }
    } else {
      if (auto n_dest = n->dest(); n_dest != nullptr) {
        // Ordinary -> Ordinary
        if (PushNode(visited, stack, *n_dest, dest)) {
          return true;
        }
      } else if (auto parent = ParentOfChildSourceNode(*n); parent != nullptr) {
        // Child source -> Meta
        if (PushNode(visited, stack, *parent, dest)) {
          return true;
        }
      }
    }

    // Pop the next node.
    for (;;) {
      if (stack.empty()) {
        return false;
      }

      n = stack.back();
      stack.pop_back();
      FX_CHECK(n != &dest);

      // PushEdge checks if a node was visited before pushing it on the stack, but it doesn't check
      // if the node is already on the stack, which means that a node might be pushed (and popped)
      // multiple times. Hence we check visited again.
      if (visited.count(n) > 0) {
        continue;
      }

      break;
    }
  }
}

}  // namespace media_audio
