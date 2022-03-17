// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/mixer_service/fidl/node.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/mixer_service/mix/pipeline_stage.h"
#include "src/media/audio/mixer_service/mix/thread.h"

namespace media_audio_mixer_service {

Node::Node(std::string_view name, bool is_meta, PipelineStagePtr pipeline_stage, NodePtr parent)
    : name_(name), is_meta_(is_meta), pipeline_stage_(std::move(pipeline_stage)), parent_(parent) {
  if (parent) {
    FX_CHECK(parent->is_meta_);
  }
  if (is_meta_) {
    FX_CHECK(!parent);           // nested meta nodes not allowed
    FX_CHECK(!pipeline_stage_);  // meta nodes cannot own PipelineStages
  } else {
    FX_CHECK(pipeline_stage_);  // each ordinary node own a PipelineStage
  }
}

const std::vector<NodePtr>& Node::inputs() const {
  FX_CHECK(!is_meta_);
  return inputs_;
}

NodePtr Node::output() const {
  FX_CHECK(!is_meta_);
  return output_;
}

const std::vector<NodePtr>& Node::child_inputs() const {
  FX_CHECK(is_meta_);
  return child_inputs_;
}

const std::vector<NodePtr>& Node::child_outputs() const {
  FX_CHECK(is_meta_);
  return child_outputs_;
}

PipelineStagePtr Node::pipeline_stage() const {
  FX_CHECK(!is_meta_);
  return pipeline_stage_;
}

ThreadPtr Node::thread() const {
  FX_CHECK(!is_meta_);
  return thread_;
}

void Node::AddInput(NodePtr n) {
  FX_CHECK(!is_meta_);
  FX_CHECK(n);
  inputs_.push_back(n);
}

void Node::SetOutput(NodePtr n) {
  FX_CHECK(!is_meta_);
  FX_CHECK(n);
  output_ = n;
}

void Node::AddChildInput(NodePtr child) {
  FX_CHECK(is_meta_);
  FX_CHECK(child);
  child_inputs_.push_back(child);
}

void Node::AddChildOutput(NodePtr child) {
  FX_CHECK(is_meta_);
  FX_CHECK(child);
  child_outputs_.push_back(child);
}

bool Node::HasInput(NodePtr n) {
  FX_CHECK(!is_meta_);
  FX_CHECK(n);

  auto it = std::find(inputs_.begin(), inputs_.end(), n);
  return it != inputs_.end();
}

void Node::RemoveInput(NodePtr n) {
  FX_CHECK(!is_meta_);
  FX_CHECK(n);

  auto it = std::find(inputs_.begin(), inputs_.end(), n);
  FX_CHECK(it != inputs_.end());
  inputs_.erase(it);
}

void Node::RemoveOutput(NodePtr n) {
  FX_CHECK(!is_meta_);
  FX_CHECK(n);
  FX_CHECK(output_ == n);

  output_ = nullptr;
}

void Node::RemoveChildInput(NodePtr child) {
  FX_CHECK(is_meta_);
  FX_CHECK(child);

  auto it = std::find(child_inputs_.begin(), child_inputs_.end(), child);
  FX_CHECK(it != child_inputs_.end());
  child_inputs_.erase(it);
}

void Node::RemoveChildOutput(NodePtr child) {
  FX_CHECK(is_meta_);
  FX_CHECK(child);

  auto it = std::find(child_outputs_.begin(), child_outputs_.end(), child);
  FX_CHECK(it != child_outputs_.end());
  child_outputs_.erase(it);
}

fpromise::result<void, fuchsia_audio_mixer::CreateEdgeError> Node::CreateEdge(
    GlobalTaskQueue& global_queue, NodePtr dest, NodePtr src) {
  FX_CHECK(dest);
  FX_CHECK(src);

  // Create a src child if needed.
  if (src->is_meta_) {
    // TODO(fxbug.dev/87651): prevent connections to dest (two children can't point to same node)
    NodePtr child = src->CreateNewChildOutput();
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kSourceHasTooManyOutputs);
    }
    auto result = CreateEdge(global_queue, dest, child);
    if (!result.is_ok()) {
      // On failure, unlink the child so it will be deleted when dropped.
      src->RemoveChildOutput(child);
    }
    return result;
  }

  // Create a dest child if needed.
  if (dest->is_meta_) {
    // TODO(fxbug.dev/87651): prevent connections to dest (two children can't point to same node)
    NodePtr child = dest->CreateNewChildInput();
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kDestHasTooManyInputs);
    }
    auto result = CreateEdge(global_queue, child, src);
    if (!result) {
      // On failure, unlink the child so it will be deleted when dropped.
      dest->RemoveChildInput(child);
    }
    return result;
  }

  if (src->output()) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
  }
  // TODO(fxbug.dev/87651): prevent connections to dest (dest.inputs can't have src twice)
  // TODO(fxbug.dev/87651): prevent duplication connection here too?
  if (!dest->CanAcceptInput(src)) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
  }
#if 0
  // TODO(fxbug.dev/87651): implement
  if (ExistsPathThroughInputs(src, dest)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kCycle);
  }
#endif

  dest->AddInput(src);
  src->SetOutput(dest);

  // TODO(fxbug.dev/87651): FX_CHECK that src->thread() is the detached thread
  // TODO(fxbug.dev/87651): update src's thread to dest->thread()

  global_queue.Push(dest->thread()->id(), [dest, src]() {
    ScopedThreadChecker checker(dest->pipeline_stage()->thread()->checker());
    dest->pipeline_stage()->AddSource(src->pipeline_stage());
  });

  return fpromise::ok();
}

fpromise::result<void, fuchsia_audio_mixer::DeleteEdgeError> Node::DeleteEdge(
    GlobalTaskQueue& global_queue, NodePtr dest, NodePtr src) {
  FX_CHECK(dest);
  FX_CHECK(src);

  if (src->is_meta_) {
    // Find src's output child that connects to dest or to a child of dest.
    NodePtr child;
    for (auto& c : src->child_outputs_) {
      if (c->output_ == dest || c->output_->parent_.lock() == dest) {
        child = c;
        break;
      }
    }
    if (!child) {
      return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
    }
    // Remove the edge child -> dest.
    // If this succeeds, then also remove child from src.
    auto result = DeleteEdge(global_queue, dest, child);
    if (result.is_ok()) {
      src->RemoveChildOutput(child);
    }
    return result;
  }

  if (dest->is_meta_) {
    // Find dest's input child that connects to src (which must be an ordinary node).
    NodePtr child;
    for (auto& c : dest->child_inputs_) {
      if (std::find(c->inputs_.begin(), c->inputs_.end(), src) != c->inputs_.end()) {
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
      dest->RemoveChildInput(child);
    }
    return result;
  }

  if (!dest->HasInput(src)) {
    return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
  }

  // If dest->HasInput(src), then this better be true as well.
  FX_CHECK(src->output_ == dest);

  src->RemoveOutput(dest);
  dest->RemoveInput(src);

  // TODO(fxbug.dev/87651): FX_CHECK that src->thread() is dest->thread()
  // TODO(fxbug.dev/87651): update src's thread to the detached thread

  global_queue.Push(dest->thread()->id(), [dest, src]() {
    ScopedThreadChecker checker(dest->pipeline_stage()->thread()->checker());
    dest->pipeline_stage()->RemoveSource(src->pipeline_stage());
  });

  return fpromise::ok();
}

}  // namespace media_audio_mixer_service
