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
#include "src/media/audio/services/mixer/mix/thread.h"

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

  return CreateEdgeInner(global_queue, std::move(source), std::move(dest));
}

fpromise::result<void, fuchsia_audio_mixer::DeleteEdgeError> Node::DeleteEdge(
    GlobalTaskQueue& global_queue, NodePtr source, NodePtr dest,
    DetachedThreadPtr detached_thread) {
  FX_CHECK(source);
  FX_CHECK(dest);

  if (source->is_meta_) {
    // Find the node in source->child_dests() that connects to dest or a child of dest.
    NodePtr child;
    for (auto& c : source->child_dests_) {
      if (c->dest_ == dest || c->dest_->parent() == dest) {
        child = c;
        break;
      }
    }
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
    }
    // Remove the edge child -> dest. This MUST succeed because we've found a child that connects
    // to dest. If this fails, there must be a bug in CreateEdge.
    const auto result = DeleteEdge(global_queue, child, dest, detached_thread);
    FX_CHECK(result.is_ok()) << "unexpected DeleteEdge(child, dest) failure with code "
                             << static_cast<int>(result.error());
    source->RemoveChildDest(child);
    return fpromise::ok();
  }

  if (dest->is_meta_) {
    // Find the node in dest->child_sources() that connects to source (which must be an ordinary
    // node).
    NodePtr child;
    for (auto& c : dest->child_sources_) {
      if (HasNode(c->sources_, source)) {
        child = c;
        break;
      }
    }
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
    }
    // Remove the edge source -> child. This MUST succeed because we've found a child that connects
    // with source. If this fails, there must be a bug in CreateEdge.
    auto result = DeleteEdge(global_queue, source, child, detached_thread);
    FX_CHECK(result.is_ok()) << "unexpected DeleteEdge(source, child) failure with code "
                             << static_cast<int>(result.error());
    dest->RemoveChildSource(child);
    return result;
  }

  if (!HasNode(dest->sources_, source)) {
    return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
  }

  // If the backwards (dest -> source) link exists, the forwards (source -> dest) link must exist
  // too.
  FX_CHECK(source->dest_ == dest);

  source->RemoveDest(dest);
  dest->RemoveSource(source);

  // Since the source was previously connected to dest, it must be owned by the same thread as dest.
  // Since the source is now disconnected, it moves to the detached thread.
  FX_CHECK(source->pipeline_stage_thread() == dest->pipeline_stage_thread());
  source->set_pipeline_stage_thread(detached_thread);

  // Save this for the closure since we can't read Nodes from the mix threads.
  const auto dest_stage_thread_id = dest->pipeline_stage_thread()->id();

  // TODO(fxbug.dev/87651): update topological order of consumer stages

  // The PipelineStages are updated asynchronously.
  global_queue.Push(dest_stage_thread_id,
                    [dest_stage = dest->pipeline_stage(),      //
                     source_stage = source->pipeline_stage(),  //
                     dest_stage_thread_id, detached_thread]() {
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

ThreadPtr Node::pipeline_stage_thread() const {
  FX_CHECK(!is_meta_);
  return pipeline_stage_thread_;
}

void Node::set_pipeline_stage_thread(ThreadPtr t) {
  FX_CHECK(!is_meta_);
  pipeline_stage_thread_ = t;
}

void Node::PrepareToDestroy() {
  if (is_meta_) {
    child_sources_.clear();
    child_dests_.clear();
  } else {
    sources_.clear();
    dest_ = nullptr;
  }
}

fpromise::result<void, fuchsia_audio_mixer::CreateEdgeError> Node::CreateEdgeInner(
    GlobalTaskQueue& global_queue, NodePtr source, NodePtr dest) {
  // Create a node in source->child_dests() if needed.
  if (source->is_meta_) {
    if (HasDestInChildren(source->child_dests(), dest)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
    }
    NodePtr child = source->CreateNewChildDest();
    if (!child) {
      return fpromise::error(
          fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
    }
    auto result = CreateEdgeInner(global_queue, child, dest);
    if (result.is_ok()) {
      source->AddChildDest(child);
    }
    return result;
  }

  // Create a node in dest->child_sources() if needed.
  if (dest->is_meta_) {
    if (HasSourceInChildren(dest->child_sources(), source)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
    }
    NodePtr child = dest->CreateNewChildSource();
    if (!child) {
      return fpromise::error(
          fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
    }
    auto result = CreateEdgeInner(global_queue, source, child);
    if (result.is_ok()) {
      dest->AddChildSource(child);
    }
    return result;
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

  // Since source->dest() is not set, source should not appear in dest->sources().
  FX_CHECK(!HasNode(dest->sources(), source));

  dest->AddSource(source);
  source->SetDest(dest);

  // Since the source was not previously connected, it must be owned by the detached thread.
  // This means we can move source to dest's thread.
  FX_CHECK(source->pipeline_stage_thread()->id() == DetachedThread::kId);
  source->set_pipeline_stage_thread(dest->pipeline_stage_thread());

  // Save this now since we can't read Nodes from the mix threads.
  const auto dest_stage_thread_id = dest->pipeline_stage_thread()->id();

  // TODO(fxbug.dev/87651): update topological order of consumer stages

  // Update the PipelineStages asynchronously, on dest's thread.
  global_queue.Push(dest_stage_thread_id,
                    [dest_stage = dest->pipeline_stage(),      //
                     source_stage = source->pipeline_stage(),  //
                     dest_stage_thread_id]() {
                      FX_CHECK(dest_stage->thread()->id() == dest_stage_thread_id)
                          << dest_stage->thread()->id() << " != " << dest_stage_thread_id;
                      FX_CHECK(source_stage->thread()->id() == DetachedThread::kId)
                          << source_stage->thread()->id() << " != " << DetachedThread::kId;

                      ScopedThreadChecker checker(dest_stage->thread()->checker());
                      source_stage->set_thread(dest_stage->thread());
                      // TODO(fxbug.dev/87651): Pass in `gain_ids`.
                      dest_stage->AddSource(source_stage, /*options=*/{});
                    });

  return fpromise::ok();
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
  FX_CHECK(child_source);
  child_sources_.push_back(std::move(child_source));
}

void Node::AddChildDest(NodePtr child_dest) {
  FX_CHECK(is_meta_);
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
  FX_CHECK(child_source);

  const auto it = std::find(child_sources_.begin(), child_sources_.end(), child_source);
  FX_CHECK(it != child_sources_.end());
  child_sources_.erase(it);
  DestroyChildSource(child_source);
}

void Node::RemoveChildDest(NodePtr child_dest) {
  FX_CHECK(is_meta_);
  FX_CHECK(child_dest);

  const auto it = std::find(child_dests_.begin(), child_dests_.end(), child_dest);
  FX_CHECK(it != child_dests_.end());
  child_dests_.erase(it);
  DestroyChildDest(child_dest);
}

}  // namespace media_audio
