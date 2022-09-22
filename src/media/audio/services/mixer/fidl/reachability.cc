// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/reachability.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <unordered_set>
#include <vector>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"

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

// Returns `n.parent()` if `n` is a child source node of a meta node. Otherwise returns nullptr.
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

zx::duration ComputeDownstreamDelay(const NodePtr& node, const NodePtr& source) {
  FX_CHECK(node);
  FX_CHECK(!node->is_meta());
  FX_CHECK(!source || !source->is_meta());

  const auto self_delay = node->GetSelfPresentationDelayForSource(source);

  if (const auto& parent = node->parent(); parent) {
    // Child of a meta node.
    const auto& child_sources = parent->child_sources();
    if (std::find(child_sources.begin(), child_sources.end(), node) != child_sources.end()) {
      // Use child destinations to compute the total downstream delay of the child source `node`.
      auto max_dest_downstream_delay = zx::nsec(0);
      for (const auto& dest : parent->child_dests()) {
        max_dest_downstream_delay =
            std::max(max_dest_downstream_delay, ComputeDownstreamDelay(dest, /*source=*/nullptr));
      }
      return self_delay + max_dest_downstream_delay;
    }
  }

  // Total downstream delay of the ordinary `node`.
  return self_delay + (node->dest() ? ComputeDownstreamDelay(node->dest(), node) : zx::nsec(0));
}

zx::duration ComputeUpstreamDelay(const NodePtr& node) {
  FX_CHECK(node);
  FX_CHECK(!node->is_meta());

  const auto* sources = &node->sources();
  if (const auto& parent = node->parent(); parent) {
    // Child of a meta node.
    const auto& child_dests = parent->child_dests();
    if (std::find(child_dests.begin(), child_dests.end(), node) != child_dests.end()) {
      // Use child sources to compute the total upstream delay of the child destination `node`.
      sources = &parent->child_sources();
    }
  }

  if (sources->empty()) {
    // No additional sources contribute to the upstream delay, return self delay directly.
    return node->GetSelfPresentationDelayForSource(nullptr);
  }

  auto max_upstream_delay = zx::nsec(0);
  for (const auto& source : *sources) {
    // Pass in `source` to compute self delay contribution iff it is part of `node->sources()`, i.e.
    // `node` is an ordinary node.
    const auto self_delay =
        node->GetSelfPresentationDelayForSource(sources == &node->sources() ? source : nullptr);
    max_upstream_delay = std::max(max_upstream_delay, self_delay + ComputeUpstreamDelay(source));
  }
  return max_upstream_delay;
}

bool ExistsPath(const Node& source, const Node& dest) {
  std::unordered_set<const Node*> visited;
  std::vector<const Node*> stack;

  const Node* n = &source;
  for (;;) {
    visited.insert(n);

    // Push each outgoing edge.
    if (n->is_meta()) {
      // Meta -> Child destination
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

      // `PushEdge` checks if a node was visited before pushing it on the stack, but it doesn't
      // check if the node is already on the stack, which means that a node might be pushed (and
      // popped) multiple times. Hence we check `visited` again.
      if (visited.count(n) > 0) {
        continue;
      }

      break;
    }
  }
}

}  // namespace media_audio
