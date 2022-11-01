// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_TESTING_FAKE_NODE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_TESTING_FAKE_NODE_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/common/global_task_queue.h"
#include "src/media/audio/services/mixer/fidl/gain_control_server.h"
#include "src/media/audio/services/mixer/fidl/graph_thread.h"
#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/testing/fake_pipeline_stage.h"
#include "src/media/audio/services/mixer/mix/testing/fake_pipeline_thread.h"

namespace media_audio {

class FakeNode;
class FakeGraphThread;
class FakeGraph;

using FakeGraphThreadPtr = std::shared_ptr<FakeGraphThread>;
using FakeNodePtr = std::shared_ptr<FakeNode>;

// A fake mix thread for use in tests.
// See FakeGraph for creation methods.
class FakeGraphThread : public GraphThread {
 public:
  // Implements `GraphThread`.
  std::shared_ptr<PipelineThread> pipeline_thread() const final { return pipeline_thread_; }
  void IncrementClockUsage(std::shared_ptr<Clock> clock) final {
    if (const auto [it, is_new] = clock_usages_.emplace(std::move(clock), 1); !is_new) {
      ++it->second;
    }
  }
  void DecrementClockUsage(std::shared_ptr<Clock> clock) final {
    if (const auto it = clock_usages_.find(clock); it != clock_usages_.end() && --it->second == 0) {
      clock_usages_.erase(it);
    }
  }

  const std::unordered_map<std::shared_ptr<Clock>, int>& clock_usages() const {
    return clock_usages_;
  }

 private:
  // All FakeGraphThreads belong to a FakeGraph. The constructor is private to ensure that it's
  // impossible to create a FakeThread which outlives its parent FakeGraph.
  friend class FakeGraph;
  FakeGraphThread(ThreadId id, std::shared_ptr<GlobalTaskQueue> global_task_queue)
      : GraphThread(std::move(global_task_queue)),
        pipeline_thread_(std::make_shared<FakePipelineThread>(id)) {}

  const std::shared_ptr<PipelineThread> pipeline_thread_;
  std::unordered_map<std::shared_ptr<Clock>, int> clock_usages_;
};

// A fake node for use in tests.
// See FakeGraph for creation methods.
//
// Not safe for concurrent use.
class FakeNode : public Node, public std::enable_shared_from_this<FakeNode> {
 public:
  // Registers a handler for `SetMaxDelays`.
  // If a handler is not registered, a default handler is used.
  void SetOnSetMaxDelays(
      std::function<std::optional<std::pair<ThreadId, fit::closure>>(Delays)> handler) {
    on_set_max_delays_ = std::move(handler);
  }

  // Registers a handler for `PresentationDelayForSourceEdge`.
  // If a handler is not registered, a default handler is used.
  void SetOnPresentationDelayForSourceEdge(std::function<zx::duration(const Node*)> handler) {
    on_presentation_delay_for_edge_ = std::move(handler);
  }

  // Registers a handler for `CreateNewChildSource`.
  // If a handler is not registered, a default handler is used.
  void SetOnCreateNewChildSource(std::function<NodePtr()> handler) {
    on_create_new_child_source_ = std::move(handler);
  }

  // Registers a handler for `CreateNewChildDest`.
  // If a handler is not registered, a default handler is used.
  void SetOnCreateNewChildDest(std::function<NodePtr()> handler) {
    on_create_new_child_dest_ = std::move(handler);
  }

  // Registers a handler for `DestroyChildSource`.
  // If a handler is not registered, a default handler is used.
  void SetOnDestroyChildSource(std::function<void(NodePtr)> handler) {
    on_destroy_child_source_ = std::move(handler);
  }

  // Registers a handler for `DestroyChildDest`.
  // If a handler is not registered, a default handler is used.
  void SetOnDestroyChildDest(std::function<void(NodePtr)> handler) {
    on_destroy_child_dest_ = std::move(handler);
  }

  // Registers a handler for `DestroySelf`.
  // If a handler is not registered, a default handler is used.
  void SetOnDestroySelf(std::function<void()> handler) { on_destroy_self_ = std::move(handler); }

  // Registers a handler for `CanAcceptSourceFormat`.
  // The default handler always returns true.
  void SetOnCanAcceptSourceFormat(std::function<bool(const Format&)> handler) {
    on_can_accept_source_format_ = std::move(handler);
  }

  // Sets the return value for `MaxSources`.
  // The default value is infinity.
  void SetMaxSources(std::optional<size_t> max_sources) { max_sources_ = max_sources; }

  // Sets the return value for `AllowsDest`.
  // The default value is true.
  void SetAllowsDest(bool b) { allows_dest_ = b; }

  // Allow anyone to set the thread.
  using Node::set_thread;

  // Our `PipelineStage` is always this type.
  FakePipelineStagePtr fake_pipeline_stage() const {
    return std::static_pointer_cast<FakePipelineStage>(pipeline_stage());
  }

  // Our `GraphThread` is always this type.
  std::shared_ptr<FakeGraphThread> fake_graph_thread() const {
    return std::static_pointer_cast<FakeGraphThread>(thread());
  }

  // Implements `Node`.
  std::optional<std::pair<ThreadId, fit::closure>> SetMaxDelays(Delays delays) final;
  zx::duration PresentationDelayForSourceEdge(const Node* source) const final;

 protected:
  NodePtr CreateNewChildSource() final;
  NodePtr CreateNewChildDest() final;
  void DestroyChildSource(NodePtr child_source) final;
  void DestroyChildDest(NodePtr child_dest) final;
  void DestroySelf() final;
  bool CanAcceptSourceFormat(const Format& format) const final;
  std::optional<size_t> MaxSources() const final { return max_sources_; }
  bool AllowsDest() const final { return allows_dest_; }

 private:
  // All FakeNodes belong to a FakeGraph. The constructor is private to ensure that it's impossible
  // to create a FakeNode which outlives its parent FakeGraph.
  friend class FakeGraph;
  FakeNode(FakeGraph& graph, NodeId id, Type type, PipelineDirection pipeline_direction,
           FakeNodePtr parent, const Format* format);

  FakeGraph& graph_;

  std::function<std::optional<std::pair<ThreadId, fit::closure>>(Delays)> on_set_max_delays_;
  std::function<zx::duration(const Node*)> on_presentation_delay_for_edge_;
  std::function<NodePtr()> on_create_new_child_source_;
  std::function<NodePtr()> on_create_new_child_dest_;
  std::function<void(NodePtr)> on_destroy_child_source_;
  std::function<void(NodePtr)> on_destroy_child_dest_;
  std::function<void()> on_destroy_self_;
  std::function<bool(const Format&)> on_can_accept_source_format_;

  std::optional<size_t> max_sources_ = std::nullopt;
  bool allows_dest_ = true;
};

// This class makes it easy to create graphs of FakeNodes during tests. For example, the following
// code:
//
// ```
//   auto graph = FakeGraph::Create({
//       .meta_nodes = {
//           {1, {
//               .source_children = {2, 3},
//               .dest_children = {4, 5},
//           }},
//       },
//       .edges = {
//           {0, 2},
//           {4, 6},
//           {5, 7},
//       },
//    });
// ```
//
// Creates a graph that looks like:
//
// ```
//     0
//     |
//   +-V-----+
//   | 2   3 |
//   |   1   |
//   | 4   5 |
//   +-|---|-+
//     V   V
//     6   7
// ```
//
// The destructor deletes all edges (to remove circular references) and drops all references to
// FakeNodes that were created by this FakeGraph.
//
// Not safe for concurrent use.
class FakeGraph {
 public:
  struct MetaNodeArgs {
    std::unordered_set<NodeId> source_children;
    std::unordered_set<NodeId> dest_children;
    bool built_in_children = false;
  };

  struct Edge {
    NodeId source;
    NodeId dest;
  };

  struct Args {
    // Set of gain controls.
    std::unordered_set<GainControlId> gain_controls;

    // Meta nodes and their children.
    std::unordered_map<NodeId, MetaNodeArgs> meta_nodes;

    // Adjaceny list.
    // All nodes must be ordinary nodes (i.e. not a key of `meta_nodes`).
    std::vector<Edge> edges;

    // Unconnected ordinary nodes.
    // These must not be mentioned in `edges`.
    std::unordered_set<NodeId> unconnected_ordinary_nodes;

    // Assignment of node types to ordinary nodes.
    // Meta nodes that are defined in `meta_nodes` are automatically assigned to
    // `Node::Type::kMeta`. All the remaining ordinary nodes that are defined in construction, but
    // not specified in this list are automatically assigned to `Node::Type::kFake`.
    std::vector<std::pair<Node::Type, std::vector<NodeId>>> types;

    // Format of data generated by each node's PipelineStage.
    // The `Format*` pointers do not need to live beyond the constructor.
    std::vector<std::pair<const Format*, std::vector<NodeId>>> formats;

    // Direction of each node.
    std::unordered_map<PipelineDirection, std::unordered_set<NodeId>> pipeline_directions;

    // The default direction if not specified above.
    PipelineDirection default_pipeline_direction = PipelineDirection::kOutput;

    // Assignment of nodes to threads. If a node is not assigned a thread, it's assigned to
    // `FakeGraph::DetachedThread()`. All nodes must be ordinary nodes (i.e. not a key of
    // `meta_nodes`).
    std::unordered_map<ThreadId, std::vector<NodeId>> threads;
  };

  explicit FakeGraph(Args args);
  ~FakeGraph();

  // Creates a thread with the given `id`.
  //
  // If `id` is unspecified, an `id` is selected automatically.
  FakeGraphThreadPtr CreateThread(std::optional<ThreadId> id);

  // Creates a meta node or return the node if the `id` already exists.
  // It is illegal to call CreateMetaNode and CreateOrdinaryNode with the same `id`.
  //
  // If `id` is unspecified, an `id` is selected automatically.
  FakeNodePtr CreateMetaNode(std::optional<NodeId> id);

  // Creates an ordinary node or return the node if `id` already exists.
  // It is illegal to call CreateMetaNode and CreateOrdinaryNode with the same `id`.
  //
  // If `id` is unspecified, an `id` is selected automatically.
  // If `parent` is specified and `id` already exists, the given `parent` must match the old parent.
  FakeNodePtr CreateOrdinaryNode(std::optional<NodeId> id, FakeNodePtr parent,
                                 Node::Type type = Node::Type::kFake);

  // Returns the thread with the given ID.
  // Must exist.
  FakeGraphThreadPtr thread(ThreadId id) const {
    auto it = threads_.find(id);
    FX_CHECK(it != threads_.end()) << "FakeGraph does have thread " << id;
    return it->second;
  }

  // Returns the node with the given ID.
  // Must exist.
  FakeNodePtr node(NodeId id) const {
    auto it = nodes_.find(id);
    FX_CHECK(it != nodes_.end()) << "FakeGraph does have node " << id;
    return it->second;
  }

  // Returns the graph context.
  const Node::GraphContext& ctx() const { return ctx_; }

  // Returns the task queue used by this FakeGraph.
  std::shared_ptr<GlobalTaskQueue> global_task_queue() const { return global_task_queue_; }

 private:
  FakeGraph(const FakeGraph&) = delete;
  FakeGraph& operator=(const FakeGraph&) = delete;

  ThreadId NextThreadId();
  NodeId NextNodeId();
  PipelineDirection PipelineDirectionForNode(NodeId id) const;

  std::unordered_map<GainControlId, std::shared_ptr<GainControlServer>> gain_controls_;
  std::unordered_map<ThreadId, FakeGraphThreadPtr> threads_;
  std::unordered_map<NodeId, FakeNodePtr> nodes_;
  std::unordered_map<NodeId, std::shared_ptr<Format>> formats_;
  std::unordered_map<PipelineDirection, std::unordered_set<NodeId>> pipeline_directions_;
  PipelineDirection default_pipeline_direction_;

  std::shared_ptr<GlobalTaskQueue> global_task_queue_ = std::make_shared<GlobalTaskQueue>();
  GraphDetachedThreadPtr detached_thread_ =
      std::make_shared<GraphDetachedThread>(global_task_queue_);

  const Node::GraphContext ctx_ = {
      .gain_controls = gain_controls_,
      .global_task_queue = *global_task_queue_,
      .detached_thread = detached_thread_,
  };
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_TESTING_FAKE_NODE_H_
