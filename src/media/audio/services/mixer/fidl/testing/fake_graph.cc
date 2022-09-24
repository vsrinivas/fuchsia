// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {

namespace {

const Format kDefaultFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kInt16, 1, 16000});

}  // namespace

FakeNode::FakeNode(FakeGraph& graph, NodeId id, bool is_meta, PipelineDirection pipeline_direction,
                   FakeNodePtr parent, const Format* format)
    : Node(std::string("Node") + std::to_string(id), is_meta, DefaultClock(), pipeline_direction,
           is_meta ? nullptr
                   : FakePipelineStage::Create({
                         .name = "PipelineStage" + std::to_string(id),
                         .format = *format,
                         .reference_clock = DefaultClock(),
                     }),
           std::move(parent)),
      graph_(graph) {}

zx::duration FakeNode::GetSelfPresentationDelayForSource(const NodePtr& source) const {
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

bool FakeNode::CanAcceptSourceFormat(const Format& format) const {
  if (on_can_accept_source_format_) {
    return on_can_accept_source_format_(format);
  }
  return true;
}

std::optional<size_t> FakeNode::MaxSources() const {
  if (on_max_sources_) {
    return on_max_sources_();
  }
  return std::nullopt;
}

bool FakeNode::AllowsDest() const {
  if (on_allows_dest_) {
    return on_allows_dest_();
  }
  return true;
}

FakeGraph::FakeGraph(Args args)
    : pipeline_directions_(std::move(args.pipeline_directions)),
      default_pipeline_direction_(args.default_pipeline_direction),
      default_thread_(args.default_thread) {
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

    for (auto id : meta_args.source_children) {
      meta->AddChildSource(CreateOrdinaryNode(id, meta));
    }
    for (auto id : meta_args.dest_children) {
      meta->AddChildDest(CreateOrdinaryNode(id, meta));
    }
  }

  // Create all edges.
  for (auto& edge : args.edges) {
    auto source = CreateOrdinaryNode(edge.source, nullptr);
    auto dest = CreateOrdinaryNode(edge.dest, nullptr);
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
    CreateOrdinaryNode(n, nullptr);
  }

  // Assign to threads.
  for (auto& [thread, node_ids] : args.threads) {
    for (auto& n : node_ids) {
      FX_CHECK(nodes_.find(n) != nodes_.end()) << "node " << n << " is not defined";
      nodes_[n]->set_pipeline_stage_thread(thread);
      nodes_[n]->fake_pipeline_stage()->set_thread(thread);
    }
  }
}

FakeGraph::~FakeGraph() {
  for (auto [id, node] : nodes_) {
    // Clear all shared_ptrs. This removes circular references so all FakeNodes can be deleted.
    node->sources_.clear();
    node->dest_ = nullptr;
    node->child_sources_.clear();
    node->child_dests_.clear();
    // Clear closures that might have additional references.
    node->on_get_self_presentation_delay_for_source_ = nullptr;
    node->on_create_new_child_source_ = nullptr;
    node->on_create_new_child_dest_ = nullptr;
    node->on_destroy_child_source_ = nullptr;
    node->on_destroy_child_dest_ = nullptr;
    node->on_can_accept_source_format_ = nullptr;
    node->on_max_sources_ = nullptr;
    node->on_allows_dest_ = nullptr;
    // Also clear PipelineStage sources. This is necessary in certain error-case tests, such as
    // tests that intentionally create cycles.
    if (!node->is_meta()) {
      auto stage = node->fake_pipeline_stage();
      while (!stage->sources().empty()) {
        stage->RemoveSource(*stage->sources().begin());
      }
    }
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

  std::shared_ptr<FakeNode> node(
      new FakeNode(*this, *id, true, PipelineDirectionForNode(*id), nullptr, nullptr));
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

  const Format* format = formats_.count(*id) ? formats_[*id].get() : &kDefaultFormat;
  const auto pipeline_direction =
      parent ? parent->pipeline_direction() : PipelineDirectionForNode(*id);

  std::shared_ptr<FakeNode> node(
      new FakeNode(*this, *id, false, pipeline_direction, parent, format));
  nodes_[*id] = node;
  node->set_pipeline_stage_thread(default_thread_);
  node->fake_pipeline_stage()->set_thread(default_thread_);
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

PipelineDirection FakeGraph::PipelineDirectionForNode(NodeId id) const {
  for (auto& [dir, nodes] : pipeline_directions_) {
    if (nodes.count(id) > 0) {
      return dir;
    }
  }
  return default_pipeline_direction_;
}

}  // namespace media_audio
