// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_NODE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_NODE_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/fpromise/result.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/common/global_task_queue.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

// Node is the base type for all nodes in the mix graph.
//
// # ORDINARY vs META NODES
//
// "Ordinary" nodes have zero or more source edges and at most one destination edge. An "ordinary
// edge" is an edge that connects two ordinary nodes.
//
// ```
//               | |
//               V V     // N.sources()
//             +-----+
//             |  N  |
//             +-----+
//                |      // N.dest()
//                V
// ```
//
// "Meta" nodes don't have direct source or destination edges. Instead they connect to other nodes
// indirectly via encapsulated "child" nodes. For example:
//
// ```
//                A
//                |
//     +----------V-----------+
//     |        +---+    Meta |
//     |        | I |         |   // Meta.child_sources()
//     |        +---+         |
//     | +----+ +----+ +----+ |
//     | | O1 | | O2 | | O3 | |   // Meta.child_dests()
//     | +----+ +----+ +----+ |
//     +---|------|------|----+
//         |      |      |
//         V      V      V
//         B      C      D
// ```
//
// For the above meta node, our graph includes the following edges:
//
// ```
// A  -> I     // A.dest() = {I}, I.sources() = {A}
// O1 -> B     // etc.
// O2 -> C
// O3 -> D
// ```
//
// We use meta nodes to represent nodes that may have more than one destination edge.
// Meta nodes cannot be nested within meta nodes. Every child node must be an ordinary node.
//
// A "meta edge" is any edge that connects a meta node to another node via the meta node's children.
// In the above example, "A->Meta", "Meta->B, "Meta->C", and "Meta->D" are meta edges. The
// separation of ordinary vs meta nodes allows us to embed "pipeline subtrees" within the DAG:
//
//   * The ordinary edges form a forest of pipeline trees
//   * The union of ordinary edges and meta edges form a DAG of nodes
//
// For more discussion on these two structures, see ../README.md.
//
// # NODE CREATION AND DELETION
//
// After creation, nodes live until there are no more references. Our DAG structure stores forwards
// and backwards pointers, which means that each edge includes cyclic references between the source
// and destination nodes. Hence, a node ill not be deleted until all of its edges are explicitly
// deleted by `DeleteEdge` calls.
//
// # THREAD SAFETY
//
// Nodes are not thread safe. Nodes must be accessed by the main FIDL thread only and should
// never be reachable from any other thread. For more information, see ../README.md.
class Node {
 public:
  // Returns the node's name. This is used for diagnostics only.
  // The name may not be a unique identifier.
  std::string_view name() const { return name_; }

  // Reports whether this is a meta node.
  bool is_meta() const { return is_meta_; }

  // Returns this ordinary node's source edges.
  // REQUIRED: !is_meta()
  const std::vector<NodePtr>& sources() const;

  // Returns this ordinary node's destination edge, or nullptr if none.
  // REQUIRED: !is_meta()
  NodePtr dest() const;

  // Returns this meta node's child source nodes.
  // REQUIRED: is_meta()
  const std::vector<NodePtr>& child_sources() const;

  // Returns this meta node's child destination nodes.
  // REQUIRED: is_meta()
  const std::vector<NodePtr>& child_dests() const;

  // Returns the parent of this node, or nullptr if this is not a child of a meta node.
  // REQUIRED: !is_meta()
  NodePtr parent() const;

  // Returns the PipelineStage owned by this node.
  // REQUIRED: !is_meta()
  PipelineStagePtr pipeline_stage() const;

  // Returns the Thread which controls this node.
  // REQUIRED: !is_meta()
  ThreadPtr thread() const;

  // Creates an edge from src -> dest. If src and dest are both ordinary nodes,
  // this creates an ordinary edge. Otherwise, this creates a meta edge: src and
  // dest will be connected indirectly through child nodes.
  //
  // Returns an error if the edge is not allowed.
  static fpromise::result<void, fuchsia_audio_mixer::CreateEdgeError> CreateEdge(
      GlobalTaskQueue& global_queue, NodePtr dest, NodePtr src);

  // Deletes the edge from src -> dest. This is the inverse of CreateEdge.
  // Returns an error if the edge does not exist.
  static fpromise::result<void, fuchsia_audio_mixer::DeleteEdgeError> DeleteEdge(
      GlobalTaskQueue& global_queue, NodePtr dest, NodePtr src);

 protected:
  // REQUIRES: `parent` outlives this node.
  Node(std::string_view name, bool is_meta, PipelineStagePtr pipeline_stage, NodePtr parent);
  virtual ~Node() = default;

  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  Node(Node&&) = delete;
  Node& operator=(Node&&) = delete;

  // Sets the Node's current thread.
  // REQUIRED: !is_meta()
  void set_thread(ThreadPtr t);

  //
  // The following methods are implementation details of CreateEdge.
  //

  // Creates an ordinary child node to accept the next source edge.
  // Returns nullptr if no more child source nodes can be created.
  // REQUIRED: is_meta()
  virtual NodePtr CreateNewChildSource() = 0;

  // Creates an ordinary child node to accept the next destination edge.
  // Returns nullptr if no more child destination nodes can be created.
  // REQUIRED: is_meta()
  virtual NodePtr CreateNewChildDest() = 0;

  // Reports whether this node can accept source from the given src node.
  // REQUIRED: !is_meta()
  virtual bool CanAcceptSource(NodePtr src) const = 0;

 private:
  friend class FakeGraph;

  // Implementation of CreateEdge.
  void AddSource(NodePtr source);
  void SetDest(NodePtr dest);
  void AddChildSource(NodePtr child_source);
  void AddChildDest(NodePtr child_dest);

  // Implementation of DeleteEdge.
  void RemoveSource(NodePtr source);
  void RemoveDest(NodePtr dest);
  void RemoveChildSource(NodePtr child_source);
  void RemoveChildDest(NodePtr child_dest);

  const std::string name_;
  const bool is_meta_;
  const PipelineStagePtr pipeline_stage_;

  // If this node is a child of a meta node, then `parent_` is that meta node.
  // This is nullptr iff there is no parent.
  const NodePtr parent_;

  // If !is_meta_.
  // To allow walking the graph in any direction, we maintain pointers in both directions.
  // Hence we have the invariant: a->HasSource(b) iff b->dest_ == a
  std::vector<NodePtr> sources_;
  NodePtr dest_;
  ThreadPtr thread_;

  // If is_meta_.
  std::vector<NodePtr> child_sources_;
  std::vector<NodePtr> child_dests_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_NODE_H_
