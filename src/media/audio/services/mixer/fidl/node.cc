// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/node.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

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

ThreadPtr Node::thread() const {
  FX_CHECK(!is_meta_);
  return thread_;
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
    GlobalTaskQueue& global_queue, NodePtr dest, NodePtr src) {
  FX_CHECK(dest);
  FX_CHECK(src);

  // Create a src child if needed.
  if (src->is_meta_) {
    if (HasDestInChildren(src->child_dests(), dest)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
    }
    NodePtr child = src->CreateNewChildDest();
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kSourceHasTooManyOutputs);
    }
    auto result = CreateEdge(global_queue, dest, child);
    if (!result.is_ok()) {
      // On failure, unlink the child so it will be deleted when dropped.
      src->RemoveChildDest(child);
    }
    return result;
  }

  // Create a dest child if needed.
  if (dest->is_meta_) {
    if (HasSourceInChildren(dest->child_sources(), src)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
    }
    NodePtr child = dest->CreateNewChildSource();
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kDestHasTooManyInputs);
    }
    auto result = CreateEdge(global_queue, child, src);
    if (!result) {
      // On failure, unlink the child so it will be deleted when dropped.
      dest->RemoveChildSource(child);
    }
    return result;
  }

  if (src->dest() || HasNode(dest->sources(), src)) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
  }

  if (!dest->CanAcceptSource(src)) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
  }
#if 0
  // TODO(fxbug.dev/87651): implement
  if (ExistsPathThroughSources(src, dest)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kCycle);
  }
#endif

  dest->AddSource(src);
  src->SetDest(dest);

  // TODO(fxbug.dev/87651): FX_CHECK that src->thread() is the detached thread
  // TODO(fxbug.dev/87651): update src's thread to dest->thread()

  global_queue.Push(dest->thread()->id(), [dest, src]() {
    ScopedThreadChecker checker(dest->pipeline_stage()->thread()->checker());
    // TODO(fxbug.dev/87651): Pass in `gain_ids`.
    dest->pipeline_stage()->AddSource(src->pipeline_stage(), {});
  });

  return fpromise::ok();
}

fpromise::result<void, fuchsia_audio_mixer::DeleteEdgeError> Node::DeleteEdge(
    GlobalTaskQueue& global_queue, NodePtr dest, NodePtr src) {
  FX_CHECK(dest);
  FX_CHECK(src);

  if (src->is_meta_) {
    // Find src's destination child that connects to dest or to a child of dest.
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
    // Remove the edge child -> dest.
    // If this succeeds, then also remove child from src.
    const auto result = DeleteEdge(global_queue, dest, child);
    if (result.is_ok()) {
      src->RemoveChildDest(child);
    }
    return result;
  }

  if (dest->is_meta_) {
    // Find dest's source child that connects to src (which must be an ordinary node).
    NodePtr child;
    for (auto& c : dest->child_sources_) {
      if (std::find(c->sources_.begin(), c->sources_.end(), src) != c->sources_.end()) {
        child = c;
        break;
      }
    }
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
    }
    // Remove the edge src -> child.
    // If this succeeds, then also remove child from dest.
    auto result = DeleteEdge(global_queue, child, src);
    if (result.is_ok()) {
      dest->RemoveChildSource(child);
    }
    return result;
  }

  if (!HasNode(dest->sources_, src)) {
    return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
  }
  FX_CHECK(src->dest_ == dest);

  src->RemoveDest(dest);
  dest->RemoveSource(src);

  // TODO(fxbug.dev/87651): FX_CHECK that src->thread() is dest->thread()
  // TODO(fxbug.dev/87651): update src's thread to the detached thread

  global_queue.Push(dest->thread()->id(), [dest, src]() {
    ScopedThreadChecker checker(dest->pipeline_stage()->thread()->checker());
    dest->pipeline_stage()->RemoveSource(src->pipeline_stage());
  });

  return fpromise::ok();
}

}  // namespace media_audio
