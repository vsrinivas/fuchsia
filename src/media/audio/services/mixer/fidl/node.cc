// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/node.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/reachability.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"
#include "src/media/audio/services/mixer/mix/pipeline_thread.h"

namespace media_audio {

namespace {

bool HasNode(const std::vector<NodePtr>& nodes, const NodePtr& node) {
  FX_CHECK(node);
  return std::find(nodes.cbegin(), nodes.cend(), node) != nodes.cend();
}

bool HasSourceInChildren(const std::vector<NodePtr>& children, const NodePtr& source) {
  FX_CHECK(source);
  // Should only be used if `source` is not a meta node (to avoid unnecessary computation).
  FX_CHECK(!source->is_meta());
  return std::find_if(children.cbegin(), children.cend(), [&source](const NodePtr& child) {
           FX_CHECK(!child->is_meta());
           return HasNode(child->sources(), source);
         }) != children.cend();
}

bool HasDestInChildren(const std::vector<NodePtr>& children, const NodePtr& dest) {
  FX_CHECK(dest);
  return std::find_if(children.cbegin(), children.cend(), [&dest](const NodePtr& child) {
           FX_CHECK(!child->is_meta());
           return dest->is_meta() ? HasNode(dest->child_sources(), child->dest())
                                  : dest == child->dest();
         }) != children.cend();
}

}  // namespace

Node::Node(std::string_view name, bool is_meta, UnreadableClock reference_clock,
           PipelineDirection pipeline_direction, PipelineStagePtr pipeline_stage, NodePtr parent)
    : name_(name),
      is_meta_(is_meta),
      reference_clock_(std::move(reference_clock)),
      pipeline_direction_(pipeline_direction),
      pipeline_stage_(std::move(pipeline_stage)),
      parent_(std::move(parent)) {
  if (parent_) {
    FX_CHECK(parent_->is_meta_);
  }
  if (is_meta_) {
    FX_CHECK(!parent_);          // nested meta nodes are not allowed
    FX_CHECK(!pipeline_stage_);  // meta nodes cannot own PipelineStages
  } else {
    FX_CHECK(pipeline_stage_);  // each ordinary node own a PipelineStage
    FX_CHECK(pipeline_stage_->reference_clock() == reference_clock_);
  }
}

fpromise::result<void, fuchsia_audio_mixer::CreateEdgeError> Node::CreateEdge(
    GlobalTaskQueue& global_queue, NodePtr source, NodePtr dest) {
  FX_CHECK(source);
  FX_CHECK(dest);

  // If there already exists a path from dest -> source, then adding source -> dest would create a
  // cycle.
  if (ExistsPath(*dest, *source)) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kCycle);
  }

  NodePtr source_parent;
  NodePtr dest_parent;

  // Create a node in source->child_dests() if needed.
  if (source->is_meta()) {
    if (HasDestInChildren(source->child_dests(), dest)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
    }
    NodePtr child = source->CreateNewChildDest();
    if (!child) {
      return fpromise::error(
          fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
    }
    source_parent = source;
    source = child;
  }

  // Create a node in dest->child_sources() if needed.
  if (dest->is_meta()) {
    if (HasSourceInChildren(dest->child_sources(), source)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
    }
    NodePtr child = dest->CreateNewChildSource();
    if (!child) {
      return fpromise::error(
          fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
    }
    dest_parent = dest;
    dest = child;
  }

  if (source->dest() == dest) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
  }
  if (source->dest() || !source->AllowsDest()) {
    return fpromise::error(
        fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
  }
  if (auto max = dest->MaxSources(); max && dest->sources().size() >= *max) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
  }
  if (!dest->CanAcceptSourceFormat(source->pipeline_stage()->format())) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
  }
  if (dest->pipeline_direction() == PipelineDirection::kOutput &&
      source->pipeline_direction() == PipelineDirection::kInput) {
    return fpromise::error(
        fuchsia_audio_mixer::CreateEdgeError::kOutputPipelineCannotReadFromInputPipeline);
  }

  // Since there is no forwards link (source -> dest), the backwards link (dest -> source) shouldn't
  // exist either.
  FX_CHECK(!HasNode(dest->sources(), source));

  // Create this edge.
  dest->AddSource(source);
  source->SetDest(dest);

  if (source_parent) {
    source_parent->AddChildDest(source);
  }
  if (dest_parent) {
    dest_parent->AddChildSource(dest);
  }

  // Since the source was not previously connected, it must be owned by the detached thread.
  // This means we can move source to dest's thread.
  FX_CHECK(source->thread()->id() == GraphDetachedThread::kId);
  source->set_thread(dest->thread());

  // TODO(fxbug.dev/87651): update topological order of consumer stages

  // Save this now since we can't read Nodes from the mix threads.
  const auto dest_stage_thread_id = dest->thread()->id();

  // Update the PipelineStages asynchronously, on dest's thread.
  global_queue.Push(dest_stage_thread_id,
                    [dest_stage = dest->pipeline_stage(),      //
                     source_stage = source->pipeline_stage(),  //
                     dest_stage_thread_id]() {
                      FX_CHECK(dest_stage->thread()->id() == dest_stage_thread_id)
                          << dest_stage->thread()->id() << " != " << dest_stage_thread_id;
                      FX_CHECK(source_stage->thread()->id() == GraphDetachedThread::kId)
                          << source_stage->thread()->id() << " != " << GraphDetachedThread::kId;

                      ScopedThreadChecker checker(dest_stage->thread()->checker());
                      source_stage->set_thread(dest_stage->thread());
                      // TODO(fxbug.dev/87651): Pass in `gain_ids`.
                      dest_stage->AddSource(source_stage, /*options=*/{});
                    });

  return fpromise::ok();
}

fpromise::result<void, fuchsia_audio_mixer::DeleteEdgeError> Node::DeleteEdge(
    GlobalTaskQueue& global_queue, GraphDetachedThreadPtr detached_thread, NodePtr source,
    NodePtr dest) {
  FX_CHECK(source);
  FX_CHECK(dest);

  NodePtr source_parent;
  NodePtr dest_parent;

  if (source->is_meta()) {
    // Find the node in `source->child_dests()` that connects to `dest` or a child of `dest`.
    NodePtr child;
    for (auto& c : source->child_dests()) {
      if (c->dest() == dest || c->dest()->parent() == dest) {
        child = c;
        break;
      }
    }
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
    }

    // Remove the edge child -> dest.
    source_parent = source;
    source = child;
  }

  if (dest->is_meta()) {
    // Find the node in `dest->child_sources()` that connects to `source`.
    NodePtr child;
    for (auto& c : dest->child_sources()) {
      if (HasNode(c->sources(), source)) {
        child = c;
        break;
      }
    }
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
    }
    // Remove the edge source -> child.
    dest_parent = dest;
    dest = child;
  }

  if (!HasNode(dest->sources(), source)) {
    return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
  }

  // The backwards link (dest -> source) exists. The forwards link (source -> dest) must exist too.
  FX_CHECK(source->dest() == dest);

  // Remove this edge.
  source->RemoveDest(dest);
  dest->RemoveSource(source);

  if (source_parent && !source_parent->built_in_children_) {
    source_parent->RemoveChildDest(source);
  }
  if (dest_parent && !dest_parent->built_in_children_ && dest->sources().empty()) {
    dest_parent->RemoveChildSource(dest);
  }

  // Since the source was previously connected to dest, it must be owned by the same thread as dest.
  // Since the source is now disconnected, it moves to the detached thread.
  FX_CHECK(source->thread() == dest->thread());
  source->set_thread(detached_thread);

  // TODO(fxbug.dev/87651): update topological order of consumer stages

  // Save this for the closure since we can't read Nodes from the mix threads.
  const auto dest_stage_thread_id = dest->thread()->id();

  // The PipelineStages are updated asynchronously.
  global_queue.Push(dest_stage_thread_id,
                    [dest_stage = dest->pipeline_stage(),      //
                     source_stage = source->pipeline_stage(),  //
                     dest_stage_thread_id, detached_thread = detached_thread->pipeline_thread()]() {
                      FX_CHECK(dest_stage->thread()->id() == dest_stage_thread_id)
                          << dest_stage->thread()->id() << " != " << dest_stage_thread_id;
                      FX_CHECK(source_stage->thread()->id() == dest_stage_thread_id)
                          << source_stage->thread()->id() << " != " << dest_stage_thread_id;

                      ScopedThreadChecker checker(dest_stage->thread()->checker());
                      source_stage->set_thread(detached_thread);
                      dest_stage->RemoveSource(source_stage);
                    });

  return fpromise::ok();
}

void Node::Destroy(GlobalTaskQueue& global_queue, GraphDetachedThreadPtr detached_thread,
                   NodePtr node) {
  // We call DeleteEdge for each existing edge. Since these edges exist, DeleteEdge cannot fail.
  auto delete_edge = [&global_queue, &detached_thread](NodePtr source, NodePtr dest) {
    // When deleting an edge A->B, if A is a dynamically-created child node, then we should delete
    // the edge [A.parent]->B to ensure we cleanup state in A.parent.
    auto lift_source_to_parent = [](NodePtr a) {
      if (a->is_meta() || !a->parent()) {
        return a;
      }
      if (auto meta = a->parent(); meta->built_in_children_) {
        return a;  // built-in node, don't lift to parent
      } else {
        return meta;  // dynamic node, lift to parent
      }
    };

    // Similarly for B->A.
    auto lift_dest_to_parent = [](NodePtr a) {
      if (a->is_meta() || !a->parent()) {
        return a;
      }
      if (auto meta = a->parent(); meta->built_in_children_) {
        return a;  // built-in node, don't lift to parent
      } else {
        return meta;  // dynamic node, lift to parent
      }
    };

    auto result = DeleteEdge(global_queue, detached_thread, lift_source_to_parent(source),
                             lift_dest_to_parent(dest));
    FX_CHECK(result.is_ok()) << result.error();
  };

  if (!node->is_meta()) {
    const auto& sources = node->sources();
    while (!sources.empty()) {
      delete_edge(sources.front(), node);
    }
    if (node->dest()) {
      delete_edge(node, node->dest());
    }

    FX_CHECK(node->sources().empty());
    FX_CHECK(node->dest() == nullptr);

  } else {
    const auto& child_sources = node->child_sources();
    const auto& child_dests = node->child_dests();

    // Iterate backwards through these vectors so as children are removed, our position stays valid.
    for (size_t k = child_sources.size(); k > 0; k--) {
      for (auto& sources = child_sources[k - 1]->sources(); !sources.empty();) {
        delete_edge(sources.front(), node);
      }
    }
    for (size_t k = child_dests.size(); k > 0; k--) {
      if (auto dest = child_dests[k - 1]->dest(); dest) {
        delete_edge(node, dest);
      }
    }

    node->child_sources_.clear();
    node->child_dests_.clear();
  }

  node->DestroySelf();
}

const std::vector<NodePtr>& Node::sources() const {
  FX_CHECK(!is_meta_);
  return sources_;
}

NodePtr Node::dest() const {
  FX_CHECK(!is_meta_);
  return dest_;
}

const std::vector<NodePtr>& Node::child_sources() const {
  FX_CHECK(is_meta_);
  return child_sources_;
}

const std::vector<NodePtr>& Node::child_dests() const {
  FX_CHECK(is_meta_);
  return child_dests_;
}

NodePtr Node::parent() const {
  FX_CHECK(!is_meta_);
  return parent_;
}

PipelineStagePtr Node::pipeline_stage() const {
  FX_CHECK(!is_meta_);
  return pipeline_stage_;
}

std::shared_ptr<GraphThread> Node::thread() const {
  FX_CHECK(!is_meta_);
  return thread_;
}

void Node::set_thread(std::shared_ptr<GraphThread> t) {
  FX_CHECK(!is_meta_);
  thread_ = t;
}

void Node::SetBuiltInChildren(std::vector<NodePtr> child_sources,
                              std::vector<NodePtr> child_dests) {
  FX_CHECK(is_meta_);
  FX_CHECK(child_sources_.empty());
  FX_CHECK(child_dests_.empty());

  child_sources_ = std::move(child_sources);
  child_dests_ = std::move(child_dests);
  built_in_children_ = true;
}

void Node::AddSource(NodePtr source) {
  FX_CHECK(!is_meta_);
  FX_CHECK(source);
  sources_.push_back(std::move(source));
}

void Node::SetDest(NodePtr dest) {
  FX_CHECK(!is_meta_);
  FX_CHECK(dest);
  dest_ = std::move(dest);
}

void Node::AddChildSource(NodePtr child_source) {
  FX_CHECK(is_meta_);
  FX_CHECK(!built_in_children_);
  FX_CHECK(child_source);
  child_sources_.push_back(std::move(child_source));
}

void Node::AddChildDest(NodePtr child_dest) {
  FX_CHECK(is_meta_);
  FX_CHECK(!built_in_children_);
  FX_CHECK(child_dest);
  child_dests_.push_back(std::move(child_dest));
}

void Node::RemoveSource(NodePtr source) {
  FX_CHECK(!is_meta_);
  FX_CHECK(source);

  const auto it = std::find(sources_.begin(), sources_.end(), source);
  FX_CHECK(it != sources_.end());
  sources_.erase(it);
}

void Node::RemoveDest(NodePtr dest) {
  FX_CHECK(!is_meta_);
  FX_CHECK(dest);
  FX_CHECK(dest_ == dest);

  dest_ = nullptr;
}

void Node::RemoveChildSource(NodePtr child_source) {
  FX_CHECK(is_meta_);
  FX_CHECK(!built_in_children_);
  FX_CHECK(child_source);

  const auto it = std::find(child_sources_.begin(), child_sources_.end(), child_source);
  FX_CHECK(it != child_sources_.end());
  child_sources_.erase(it);
  DestroyChildSource(child_source);
}

void Node::RemoveChildDest(NodePtr child_dest) {
  FX_CHECK(is_meta_);
  FX_CHECK(!built_in_children_);
  FX_CHECK(child_dest);

  const auto it = std::find(child_dests_.begin(), child_dests_.end(), child_dest);
  FX_CHECK(it != child_dests_.end());
  child_dests_.erase(it);
  DestroyChildDest(child_dest);
}

}  // namespace media_audio
