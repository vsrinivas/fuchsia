// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {

namespace {
PipelineStagePtr CreatePipelineStage() {
  const Format format =
      Format::CreateOrDie({fuchsia_mediastreams::wire::AudioSampleFormat::kFloat, 2, 48000});
  return std::make_shared<SimplePacketQueueProducerStage>(SimplePacketQueueProducerStage::Args{
      .format = format,
      .reference_clock_koid = DefaultClockKoid(),
  });
}
}  // namespace

FakeNode::FakeNode(FakeGraph& graph, NodeId id, bool is_meta, FakeNodePtr parent)
    : Node(std::string("Node") + std::to_string(id), is_meta,
           is_meta ? nullptr : CreatePipelineStage(), parent),
      graph_(graph) {}

NodePtr FakeNode::CreateNewChildInput() {
  if (on_create_new_child_input_) {
    return (*on_create_new_child_input_)();
  }
  return graph_.CreateOrdinaryNode(std::nullopt, shared_from_this());
}

NodePtr FakeNode::CreateNewChildOutput() {
  if (on_create_new_child_output_) {
    return (*on_create_new_child_output_)();
  }
  return graph_.CreateOrdinaryNode(std::nullopt, shared_from_this());
}

bool FakeNode::CanAcceptInput(NodePtr src) const {
  if (on_can_accept_input_) {
    return (*on_can_accept_input_)(src);
  }
  return true;
}

FakeGraph::FakeGraph(Args args) {
  // Create all meta nodes and their children.
  for (auto& [meta_id, meta_args] : args.meta_nodes) {
    auto meta = CreateMetaNode(meta_id);

    for (auto id : meta_args.input_children) {
      meta->AddChildInput(CreateOrdinaryNode(id, meta));
    }
    for (auto id : meta_args.output_children) {
      meta->AddChildOutput(CreateOrdinaryNode(id, meta));
    }
  }

  // Create all edges.
  for (auto& edge : args.edges) {
    auto src = CreateOrdinaryNode(edge.src, nullptr);
    auto dest = CreateOrdinaryNode(edge.dest, nullptr);
    // Ordinary nodes can have at most one output.
    if (src->output()) {
      FX_CHECK(src->output() == dest)
          << src->name() << " has ambiguous output: " << src->output()->name() << " vs "
          << dest->name();
    }
    src->SetOutput(dest);
    dest->AddInput(src);
  }
}

FakeGraph::~FakeGraph() {
  for (auto [id, node] : nodes_) {
    node->inputs_.clear();
    node->output_ = nullptr;
    node->child_inputs_.clear();
    node->child_outputs_.clear();
  }
}

FakeNodePtr FakeGraph::CreateMetaNode(std::optional<NodeId> id) {
  if (id) {
    if (auto it = nodes_.find(*id); it != nodes_.end()) {
      FX_CHECK(it->second->is_meta())
          << "node " << *id << " cannot be both a meta and ordinary node";
      return it->second;
    }
  } else {
    id = NextId();
  }

  std::shared_ptr<FakeNode> node(new FakeNode(*this, *id, true, nullptr));
  nodes_[*id] = node;
  return node;
}

FakeNodePtr FakeGraph::CreateOrdinaryNode(std::optional<NodeId> id, FakeNodePtr parent) {
  if (id) {
    if (auto it = nodes_.find(*id); it != nodes_.end()) {
      FX_CHECK(!it->second->is_meta())
          << "node " << *id << " cannot be both a meta and ordinary node";
      // If parent is specified, it must match.
      if (parent) {
        if (auto other_parent = it->second->parent(); other_parent) {
          FX_CHECK(other_parent.get() == parent.get()) << "node " << *id << " has ambiguous parent";
        }
      }
      return it->second;
    }
  } else {
    id = NextId();
  }

  std::shared_ptr<FakeNode> node(new FakeNode(*this, *id, false, parent));
  nodes_[*id] = node;
  return node;
}

NodeId FakeGraph::NextId() {
  // Since the Create*Node methods can create nodes with arbitrary IDs, we can't guarantee that IDs
  // are densely monotonically increasing (0,1,2,...), so we need to go searching for an unused ID.
  NodeId id = nodes_.size();
  while (nodes_.count(id) > 0) {
    id++;
  }
  return id;
}

}  // namespace media_audio
