// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <unordered_map>

#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {

namespace {

const Format kDefaultFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kInt16, 1, 16000});

Node::Type NodeTypeFromId(const std::unordered_map<NodeId, Node::Type>& types, NodeId id) {
  if (const auto it = types.find(id); it != types.end()) {
    return it->second;
  }
  return Node::Type::kFake;
}

}  // namespace

FakeNode::FakeNode(FakeGraph& graph, NodeId id, Type type, PipelineDirection pipeline_direction,
                   FakeNodePtr parent, const Format* format)
    : Node(type, std::string("Node") + std::to_string(id), DefaultClock(), pipeline_direction,
           type == Node::Type::kMeta ? nullptr
                                     : FakePipelineStage::Create({
                                           .name = "PipelineStage" + std::to_string(id),
                                           .format = *format,
                                           .reference_clock = DefaultUnreadableClock(),
                                       }),
           std::move(parent)),
      graph_(graph) {}

zx::duration FakeNode::GetSelfPresentationDelayForSource(const Node* source) const {
  if (on_get_self_presentation_delay_for_source_) {
    return on_get_self_presentation_delay_for_source_(source);
  }
  return zx::nsec(0);
}

NodePtr FakeNode::CreateNewChildSource() {
  if (on_create_new_child_source_) {
    return on_create_new_child_source_();
  }
  return graph_.CreateOrdinaryNode(std::nullopt, shared_from_this());
}

NodePtr FakeNode::CreateNewChildDest() {
  if (on_create_new_child_dest_) {
    return on_create_new_child_dest_();
  }
  return graph_.CreateOrdinaryNode(std::nullopt, shared_from_this());
}

void FakeNode::DestroyChildSource(NodePtr child_source) {
  if (on_destroy_child_source_) {
    on_destroy_child_source_(child_source);
  }
}

void FakeNode::DestroyChildDest(NodePtr child_dest) {
  if (on_destroy_child_dest_) {
    on_destroy_child_dest_(child_dest);
  }
}

void FakeNode::DestroySelf() {
  if (on_destroy_self_) {
    on_destroy_self_();
  }
}

bool FakeNode::CanAcceptSourceFormat(const Format& format) const {
  if (on_can_accept_source_format_) {
    return on_can_accept_source_format_(format);
  }
  return true;
}

FakeGraph::FakeGraph(Args args)
    : pipeline_directions_(std::move(args.pipeline_directions)),
      default_pipeline_direction_(args.default_pipeline_direction),
      global_task_queue_(std::make_shared<GlobalTaskQueue>()),
      detached_thread_(std::make_shared<GraphDetachedThread>(global_task_queue_)) {
  // Populate `types`.
  std::unordered_map<NodeId, Node::Type> types;
  for (const auto& [type, nodes] : args.types) {
    for (auto id : nodes) {
      types[id] = type;
    }
  }

  // Populate `formats_`.
  for (auto& [format_ptr, nodes] : args.formats) {
    auto format = std::make_shared<Format>(*format_ptr);
    for (auto id : nodes) {
      formats_[id] = format;
    }
  }

  // Create all meta nodes and their children.
  for (auto& [meta_id, meta_args] : args.meta_nodes) {
    auto meta = CreateMetaNode(meta_id);

    if (meta_args.built_in_children) {
      std::vector<NodePtr> builtin_sources;
      for (auto id : meta_args.source_children) {
        builtin_sources.push_back(CreateOrdinaryNode(id, meta, NodeTypeFromId(types, id)));
      }

      std::vector<NodePtr> builtin_dests;
      for (auto id : meta_args.dest_children) {
        builtin_dests.push_back(CreateOrdinaryNode(id, meta, NodeTypeFromId(types, id)));
      }

      meta->SetBuiltInChildren(std::move(builtin_sources), std::move(builtin_dests));
      continue;
    }

    for (auto id : meta_args.source_children) {
      meta->AddChildSource(CreateOrdinaryNode(id, meta, NodeTypeFromId(types, id)));
    }
    for (auto id : meta_args.dest_children) {
      meta->AddChildDest(CreateOrdinaryNode(id, meta, NodeTypeFromId(types, id)));
    }
  }

  // Create all edges.
  for (auto& edge : args.edges) {
    auto source = CreateOrdinaryNode(edge.source, nullptr, NodeTypeFromId(types, edge.source));
    auto dest = CreateOrdinaryNode(edge.dest, nullptr, NodeTypeFromId(types, edge.dest));
    // Ordinary nodes can have at most one destination.
    if (source->dest()) {
      FX_CHECK(source->dest() == dest)
          << source->name() << " has ambiguous destination: " << source->dest()->name() << " vs "
          << dest->name();
    }
    source->SetDest(dest);
    dest->AddSource(source);
    dest->pipeline_stage()->AddSource(source->pipeline_stage(), {});
  }

  // Create all unconnected nodes.
  // Since so far we've created all connected ordinary nodes, and these are expected to be
  // unconnected, none of these nodes should exist yet.
  for (auto& n : args.unconnected_ordinary_nodes) {
    FX_CHECK(nodes_.find(n) == nodes_.end()) << "node " << n << " already created";
    CreateOrdinaryNode(n, nullptr, NodeTypeFromId(types, n));
  }

  // Assign to threads.
  for (auto& [thread_id, node_ids] : args.threads) {
    auto thread = threads_.count(thread_id) ? threads_[thread_id] : CreateThread(thread_id);
    for (auto& n : node_ids) {
      FX_CHECK(nodes_.find(n) != nodes_.end()) << "node " << n << " is not defined";
      nodes_[n]->set_thread(thread);
      nodes_[n]->fake_pipeline_stage()->set_thread(thread->pipeline_thread());
    }
  }
}

FakeGraph::~FakeGraph() {
  for (auto [id, node] : nodes_) {
    // Clear closures that might have additional references.
    node->on_get_self_presentation_delay_for_source_ = nullptr;
    node->on_create_new_child_source_ = nullptr;
    node->on_create_new_child_dest_ = nullptr;
    node->on_destroy_child_source_ = nullptr;
    node->on_destroy_child_dest_ = nullptr;
    node->on_destroy_self_ = nullptr;
    node->on_can_accept_source_format_ = nullptr;
    // Remove all circular references so that every FakeNode and FakePipelineStage can be deleted.
    // Do this after clearing closures so the closures don't run.
    Node::Destroy(*global_task_queue_, detached_thread_, node);
    // Also clear PipelineStage sources. This is necessary in certain error-case tests, such as
    // tests that intentionally create cycles.
    if (node->type() != Node::Type::kMeta) {
      auto stage = node->fake_pipeline_stage();
      while (!stage->sources().empty()) {
        stage->RemoveSource(*stage->sources().begin());
      }
    }
  }
}

FakeGraphThreadPtr FakeGraph::CreateThread(std::optional<ThreadId> id) {
  if (!id) {
    id = NextThreadId();
  }
  std::shared_ptr<FakeGraphThread> thread(new FakeGraphThread(*id, global_task_queue_));
  threads_[*id] = thread;
  return thread;
}

FakeNodePtr FakeGraph::CreateMetaNode(std::optional<NodeId> id) {
  if (id) {
    if (auto it = nodes_.find(*id); it != nodes_.end()) {
      FX_CHECK(it->second->type() == Node::Type::kMeta)
          << "node " << *id << " cannot be both a meta and ordinary node";
      return it->second;
    }
  } else {
    id = NextNodeId();
  }

  std::shared_ptr<FakeNode> node(
      new FakeNode(*this, *id, Node::Type::kMeta, PipelineDirectionForNode(*id), nullptr, nullptr));
  nodes_[*id] = node;
  return node;
}

FakeNodePtr FakeGraph::CreateOrdinaryNode(std::optional<NodeId> id, FakeNodePtr parent,
                                          Node::Type type) {
  if (id) {
    if (auto it = nodes_.find(*id); it != nodes_.end()) {
      FX_CHECK(it->second->type() != Node::Type::kMeta)
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
    id = NextNodeId();
  }

  const Format* format = formats_.count(*id) ? formats_[*id].get() : &kDefaultFormat;
  const auto pipeline_direction =
      parent ? parent->pipeline_direction() : PipelineDirectionForNode(*id);

  std::shared_ptr<FakeNode> node(
      new FakeNode(*this, *id, type, pipeline_direction, parent, format));
  nodes_[*id] = node;
  node->set_thread(detached_thread_);
  node->fake_pipeline_stage()->set_thread(detached_thread_->pipeline_thread());
  return node;
}

ThreadId FakeGraph::NextThreadId() {
  // Since CreateThread can create nodes with arbitrary IDs, we can't guarantee that IDs are densely
  // monotonically increasing (0,1,2,...), so we need to go searching for an unused ID.
  ThreadId id = threads_.size();
  while (threads_.count(id) > 0) {
    id++;
  }
  return id;
}

NodeId FakeGraph::NextNodeId() {
  // Since the Create*Node methods can create nodes with arbitrary IDs, we can't guarantee that IDs
  // are densely monotonically increasing (0,1,2,...), so we need to go searching for an unused ID.
  NodeId id = nodes_.size();
  while (nodes_.count(id) > 0) {
    id++;
  }
  return id;
}

PipelineDirection FakeGraph::PipelineDirectionForNode(NodeId id) const {
  for (auto& [dir, nodes] : pipeline_directions_) {
    if (nodes.count(id) > 0) {
      return dir;
    }
  }
  return default_pipeline_direction_;
}

}  // namespace media_audio
