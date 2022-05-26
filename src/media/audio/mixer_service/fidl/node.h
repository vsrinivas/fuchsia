// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_NODE_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_NODE_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/fpromise/result.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "src/media/audio/mixer_service/common/basic_types.h"
#include "src/media/audio/mixer_service/common/global_task_queue.h"
#include "src/media/audio/mixer_service/fidl/ptr_decls.h"
#include "src/media/audio/mixer_service/mix/ptr_decls.h"

namespace media_audio_mixer_service {

// Node is the base type for all nodes in the mix graph.
//
// ORDINARY vs META NODES
//
// "Ordinary" nodes have zero or more input edges and at most one output edge. An "ordinary edge"
// is an edge that connects two ordinary nodes.
//
//                | |
//                V V     // N.inputs()
//              +-----+
//              |  N  |
//              +-----+
//                 |      // N.output()
//                 V
//
// "Meta" nodes don't have direct input or output edges. Instead they connect to other nodes
// indirectly via encapsulated "child" nodes. For example:
//
//                A
//                |
//     +----------V-----------+
//     |        +---+    Meta |
//     |        | I |         |   // Meta.child_inputs()
//     |        +---+         |
//     | +----+ +----+ +----+ |
//     | | O1 | | O2 | | O3 | |   // Meta.child_outputs()
//     | +----+ +----+ +----+ |
//     +---|------|------|----+
//         |      |      |
//         V      V      V
//         B      C      D
//
// For the above meta node, our graph includes the following edges:
//
//   A  -> I     // A.outputs() = {I}, I.inputs() = {A}
//   O1 -> B     // etc.
//   O2 -> C
//   O3 -> D
//
// We use meta nodes to represent nodes that may have more than one output edge.
// Meta nodes cannot be nested within meta nodes. Every child node must be an ordinary node.
//
// A "meta edge" is any edge that connects a meta node to another node via the meta node's children.
// In the above example, "A->Meta", "Meta->B, "Meta->C", and "Meta->D" are meta edges. The
// separation of ordinary vs meta nodes allows us to embed "pipeline subtrees" within the DAG:
//
//   * The ordinary edges form a forest of pipeline trees
//   * The ordinary edges combined with meta edges form a DAG of nodes
//
// For more discussion on these two structures, see ../README.md.
//
// OWNERSHIP
//
// Each ordinary node owns a PipelineStage.
// Each meta node owns its child nodes.
//
// THREAD SAFETY
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

  // Returns this ordinary node's input edges.
  // REQUIRED: !is_meta()
  const std::vector<NodePtr>& inputs() const;

  // Returns this ordinary node's output edge, or nullptr if none.
  // REQUIRED: !is_meta()
  NodePtr output() const;

  // Returns this meta node's child input nodes.
  // REQUIRED: is_meta()
  const std::vector<NodePtr>& child_inputs() const;

  // Returns this meta node's child output nodes.
  // REQUIRED: is_meta()
  const std::vector<NodePtr>& child_outputs() const;

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

  // Creates an ordinary child node to accept the next input edge.
  // Returns nullptr if no more child input nodes can be created.
  // REQUIRED: is_meta()
  virtual NodePtr CreateNewChildInput() = 0;

  // Creates an ordinary child node to accept the next output edge.
  // Returns nullptr if no more child output nodes can be created.
  // REQUIRED: is_meta()
  virtual NodePtr CreateNewChildOutput() = 0;

  // Reports whether this node can accept input from the given src node.
  // REQUIRED: !is_meta()
  virtual bool CanAcceptInput(NodePtr src) const = 0;

 private:
  friend class FakeGraph;

  // Implementation of CreateEdge.
  void AddInput(NodePtr input);
  void SetOutput(NodePtr output);
  void AddChildInput(NodePtr child_input);
  void AddChildOutput(NodePtr child_output);

  // Implementation of DeleteEdge.
  void RemoveInput(NodePtr input);
  void RemoveOutput(NodePtr output);
  void RemoveChildInput(NodePtr child_input);
  void RemoveChildOutput(NodePtr child_output);

  const std::string name_;
  const bool is_meta_;
  const PipelineStagePtr pipeline_stage_;

  // If this node is a child of a meta node, then `parent_` is that meta node.
  // This is held as a weak_ptr to avoid a reference counting cycle.
  // This is std::nullopt iff there is no parent.
  const std::optional<std::weak_ptr<Node>> parent_;

  // If !is_meta_.
  // To allow walking the graph in any direction, we maintain both inputs and outputs.
  // Hence we have the invariant: a->HasInput(b) iff b->output_ == a
  std::vector<NodePtr> inputs_;
  NodePtr output_;
  ThreadPtr thread_;

  // If is_meta_.
  std::vector<NodePtr> child_inputs_;
  std::vector<NodePtr> child_outputs_;
};

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_NODE_H_
