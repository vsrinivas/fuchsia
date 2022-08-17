// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/node.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

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

Node::Node(std::string_view name, bool is_meta, PipelineStagePtr pipeline_stage, NodePtr parent)
    : name_(name),
      is_meta_(is_meta),
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
  }
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
}

void Node::RemoveChildDest(NodePtr child_dest) {
  FX_CHECK(is_meta_);
  FX_CHECK(child_dest);

  const auto it = std::find(child_dests_.begin(), child_dests_.end(), child_dest);
  FX_CHECK(it != child_dests_.end());
  child_dests_.erase(it);
}

fpromise::result<void, fuchsia_audio_mixer::CreateEdgeError> Node::CreateEdge(
    GlobalTaskQueue& global_queue, NodePtr src, NodePtr dest) {
  FX_CHECK(src);
  FX_CHECK(dest);

  // If there already exists a path from dest -> src, then adding src -> dest would create a cycle.
  if (ExistsPath(*dest, *src)) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kCycle);
  }

  return CreateEdgeInner(global_queue, std::move(src), std::move(dest));
}

fpromise::result<void, fuchsia_audio_mixer::CreateEdgeError> Node::CreateEdgeInner(
    GlobalTaskQueue& global_queue, NodePtr src, NodePtr dest) {
  // Create a node in src->child_dests() if needed.
  if (src->is_meta_) {
    if (HasDestInChildren(src->child_dests(), dest)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
    }
    NodePtr child = src->CreateNewChildDest();
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kSourceHasTooManyOutputs);
    }
    auto result = CreateEdgeInner(global_queue, child, dest);
    if (result.is_ok()) {
      src->AddChildDest(child);
    }
    return result;
  }

  // Create a node in dest->child_sources() if needed.
  if (dest->is_meta_) {
    if (HasSourceInChildren(dest->child_sources(), src)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
    }
    NodePtr child = dest->CreateNewChildSource();
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kDestHasTooManyInputs);
    }
    auto result = CreateEdgeInner(global_queue, src, child);
    if (result.is_ok()) {
      dest->AddChildSource(child);
    }
    return result;
  }

  if (src->dest()) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
  }
  if (!dest->CanAcceptSource(src)) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
  }

  // Since src->dest() is not set, src should not appear in dest->sources().
  FX_CHECK(!HasNode(dest->sources(), src));

  dest->AddSource(src);
  src->SetDest(dest);

  // Since the source was not previously connected, it must be owned by the detached thread.
  // This means we can move src to dest's thread.
  FX_CHECK(src->pipeline_stage_thread()->id() == DetachedThread::kId);
  src->set_pipeline_stage_thread(dest->pipeline_stage_thread());

  // Save this now since we can't read Nodes from the mix threads.
  const auto dest_stage_thread_id = dest->pipeline_stage_thread()->id();

  // Update the PipelineStages asynchronously, on dest's thread.
  global_queue.Push(dest_stage_thread_id,
                    [dest_stage = dest->pipeline_stage(),  //
                     src_stage = src->pipeline_stage(),    //
                     dest_stage_thread_id]() {
                      FX_CHECK(dest_stage->thread()->id() == dest_stage_thread_id)
                          << dest_stage->thread()->id() << " != " << dest_stage_thread_id;
                      FX_CHECK(src_stage->thread()->id() == DetachedThread::kId)
                          << src_stage->thread()->id() << " != " << DetachedThread::kId;

                      ScopedThreadChecker checker(dest_stage->thread()->checker());
                      src_stage->set_thread(dest_stage->thread());
                      // TODO(fxbug.dev/87651): Pass in `gain_ids`.
                      dest_stage->AddSource(src_stage, /*options=*/{});
                    });

  return fpromise::ok();
}

fpromise::result<void, fuchsia_audio_mixer::DeleteEdgeError> Node::DeleteEdge(
    GlobalTaskQueue& global_queue, NodePtr src, NodePtr dest, DetachedThreadPtr detached_thread) {
  FX_CHECK(src);
  FX_CHECK(dest);

  if (src->is_meta_) {
    // Find the node in src->child_dests() that connects to dest or a child of dest.
    NodePtr child;
    for (auto& c : src->child_dests_) {
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
    src->RemoveChildDest(child);
    return fpromise::ok();
  }

  if (dest->is_meta_) {
    // Find the node in dest->child_sources() that connects to src (which must be an ordinary node).
    NodePtr child;
    for (auto& c : dest->child_sources_) {
      if (HasNode(c->sources_, src)) {
        child = c;
        break;
      }
    }
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
    }
    // Remove the edge src -> child. This MUST succeed because we've found a child that connects
    // with src. If this fails, there must be a bug in CreateEdge.
    auto result = DeleteEdge(global_queue, src, child, detached_thread);
    FX_CHECK(result.is_ok()) << "unexpected DeleteEdge(src, child) failure with code "
                             << static_cast<int>(result.error());
    dest->RemoveChildSource(child);
    return result;
  }

  if (!HasNode(dest->sources_, src)) {
    return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
  }

  // If the backwards (dest -> src) link exists, the forwards (src -> dest) link must exist too.
  FX_CHECK(src->dest_ == dest);

  src->RemoveDest(dest);
  dest->RemoveSource(src);

  // Since the source was previously connected to dest, it must be owned by the same thread as dest.
  // Since the source is now disconnected, it moves to the detached thread.
  FX_CHECK(src->pipeline_stage_thread() == dest->pipeline_stage_thread());
  src->set_pipeline_stage_thread(detached_thread);

  // Save this for the closure since we can't read Nodes from the mix threads.
  const auto dest_stage_thread_id = dest->pipeline_stage_thread()->id();

  // The PipelineStages are updated asynchronously.
  global_queue.Push(dest_stage_thread_id,
                    [dest_stage = dest->pipeline_stage(),  //
                     src_stage = src->pipeline_stage(),    //
                     dest_stage_thread_id, detached_thread]() {
                      FX_CHECK(dest_stage->thread()->id() == dest_stage_thread_id)
                          << dest_stage->thread()->id() << " != " << dest_stage_thread_id;
                      FX_CHECK(src_stage->thread()->id() == dest_stage_thread_id)
                          << src_stage->thread()->id() << " != " << dest_stage_thread_id;

                      ScopedThreadChecker checker(dest_stage->thread()->checker());
                      src_stage->set_thread(detached_thread);
                      dest_stage->RemoveSource(src_stage);
                    });

  return fpromise::ok();
}

}  // namespace media_audio
