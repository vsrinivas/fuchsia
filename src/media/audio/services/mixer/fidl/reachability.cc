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

// Runs `fn` for all ordinary nodes immediately downstream of `node`, which must be an ordinary
// node. The `fn` must have type `void(const Node&)` or `void(Node&)`. If `node` is a child_source
// of a meta node, the meta node's child_dest nodes are "immediately downstream".
template <typename Fn>
void ForEachDownstreamEdge(const Node& node, Fn fn) {
  FX_CHECK(node.type() != Node::Type::kMeta);

  // Three cases:
  //
  // 1. node does not have a parent follow the direct edge.
  // 2. node is a child_dest of a meta node: follow the direct edge.
  // 3. node is a child_source of a meta node: follow implicit edges to the child_dests.

  // Cases 1 & 2.
  if (node.dest()) {
    fn(*node.dest());
    return;
  }

  // Case 3.
  const auto& parent = node.parent();
  if (!parent) {
    return;
  }
  const auto& child_sources = parent->child_sources();
  if (std::find_if(child_sources.begin(), child_sources.end(),
                   [&node](auto x) { return x.get() == &node; }) == child_sources.end()) {
    return;
  }
  for (const auto& dest : parent->child_dests()) {
    fn(*dest);
  }
}

// Runs `fn` for all ordinary nodess immediately upstream of `node`, which must be an ordinary node.
// The `fn` must have type `void(const Node&)` or `void(Node&)`. If `node` is a child_dest of a meta
// node, the meta node's child_source nodes are "immediate upstream".
template <typename Fn>
void ForEachUpstreamEdge(const Node& node, Fn fn) {
  FX_CHECK(node.type() != Node::Type::kMeta);

  // Three cases:
  //
  // 1. node does not have a parent follow the direct edges.
  // 2. node is a child_source of a meta node: follow the direct edges.
  // 3. node is a child_dest of a meta node: follow implicit edges to the child_sources.

  // Case 1 & 2.
  const auto* sources = &node.sources();

  // Case 3.
  if (sources->empty()) {
    if (const auto& parent = node.parent(); parent) {
      const auto& child_dests = parent->child_dests();
      if (std::find_if(child_dests.begin(), child_dests.end(),
                       [&node](auto x) { return x.get() == &node; }) != child_dests.end()) {
        sources = &parent->child_sources();
      }
    }
  }

  for (const auto& source : *sources) {
    fn(*source);
  }
}

}  // namespace

zx::duration ComputeDownstreamDelay(const Node& node, const Node* source) {
  FX_CHECK(node.type() != Node::Type::kMeta);
  FX_CHECK(!source || source->type() != Node::Type::kMeta);

  const auto self_delay = node.GetSelfPresentationDelayForSource(source);
  auto max_downstream_delay = zx::nsec(0);

  ForEachDownstreamEdge(node, [&max_downstream_delay, &node](const Node& dest) {
    max_downstream_delay =
        std::max(max_downstream_delay,
                 ComputeDownstreamDelay(dest, (node.dest().get() == &dest) ? &node : nullptr));
  });

  return self_delay + max_downstream_delay;
}

zx::duration ComputeUpstreamDelay(const Node& node) {
  FX_CHECK(node.type() != Node::Type::kMeta);

  auto max_upstream_delay = zx::nsec(0);
  int64_t num_sources = 0;

  ForEachUpstreamEdge(node, [&max_upstream_delay, &num_sources, &node](const Node& source) {
    num_sources++;
    // Pass in `source` to compute self delay contribution iff it is part of `node.sources()`.
    const auto self_delay =
        node.GetSelfPresentationDelayForSource(!node.sources().empty() ? &source : nullptr);
    max_upstream_delay = std::max(max_upstream_delay, self_delay + ComputeUpstreamDelay(source));
  });

  if (num_sources > 0) {
    return max_upstream_delay;
  }

  return node.GetSelfPresentationDelayForSource(nullptr);
}

bool ExistsPath(const Node& source, const Node& dest) {
  std::unordered_set<const Node*> visited;
  std::vector<const Node*> stack;

  const Node* n = &source;
  for (;;) {
    visited.insert(n);

    // Push each outgoing edge.
    if (n->type() == Node::Type::kMeta) {
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

std::vector<PipelineStagePtr> MoveNodeToThread(Node& node, std::shared_ptr<GraphThread> new_thread,
                                               std::shared_ptr<GraphThread> expected_thread) {
  std::vector<PipelineStagePtr> out;
  std::vector<Node*> stack = {&node};

  while (!stack.empty()) {
    auto node = stack.back();
    stack.pop_back();

    FX_CHECK(node->thread() == expected_thread)
        << "Node " << node->name() << " expected on thread " << expected_thread->name()
        << ", found on thread " << node->thread()->name();

    node->set_thread(new_thread);
    out.push_back(node->pipeline_stage());

    ForEachUpstreamEdge(*node, [&stack](Node& source) {
      if (source.type() != Node::Type::kConsumer) {
        stack.push_back(&source);
      }
    });
  }

  return out;
}

}  // namespace media_audio
