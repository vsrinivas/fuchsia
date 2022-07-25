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

bool HasInputInChildren(const std::vector<NodePtr>& children, const NodePtr& input) {
  FX_CHECK(input);
  // Should only be used if `input` is not a meta node (to avoid unnecessary computation).
  FX_CHECK(!input->is_meta());
  return std::find_if(children.cbegin(), children.cend(), [&input](const NodePtr& child) {
           FX_CHECK(!child->is_meta());
           return HasNode(child->inputs(), input);
         }) != children.cend();
}

bool HasOutputInChildren(const std::vector<NodePtr>& children, const NodePtr& output) {
  FX_CHECK(output);
  return std::find_if(children.cbegin(), children.cend(), [&output](const NodePtr& child) {
           FX_CHECK(!child->is_meta());
           return output->is_meta() ? HasNode(output->child_inputs(), child->output())
                                    : output == child->output();
         }) != children.cend();
}

}  // namespace

Node::Node(std::string_view name, bool is_meta, PipelineStagePtr pipeline_stage, NodePtr parent)
    : name_(name),
      is_meta_(is_meta),
      pipeline_stage_(std::move(pipeline_stage)),
      parent_(parent ? std::optional<std::weak_ptr<Node>>(parent) : std::nullopt) {
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

NodePtr Node::parent() const {
  FX_CHECK(!is_meta_);
  if (!parent_) {
    return nullptr;
  }
  // Although `parent_` is a weak_ptr to avoid reference counting cycles, if this node has a parent,
  // then the parent is a meta node which must outlive this child node. Hence, `lock` cannot fail.
  auto p = parent_->lock();
  FX_CHECK(p) << "node cannot outlive its parent";
  return p;
}

PipelineStagePtr Node::pipeline_stage() const {
  FX_CHECK(!is_meta_);
  return pipeline_stage_;
}

ThreadPtr Node::thread() const {
  FX_CHECK(!is_meta_);
  return thread_;
}

void Node::AddInput(NodePtr input) {
  FX_CHECK(!is_meta_);
  FX_CHECK(input);
  inputs_.push_back(std::move(input));
}

void Node::SetOutput(NodePtr output) {
  FX_CHECK(!is_meta_);
  FX_CHECK(output);
  output_ = std::move(output);
}

void Node::AddChildInput(NodePtr child_input) {
  FX_CHECK(is_meta_);
  FX_CHECK(child_input);
  child_inputs_.push_back(std::move(child_input));
}

void Node::AddChildOutput(NodePtr child_output) {
  FX_CHECK(is_meta_);
  FX_CHECK(child_output);
  child_outputs_.push_back(std::move(child_output));
}

void Node::RemoveInput(NodePtr input) {
  FX_CHECK(!is_meta_);
  FX_CHECK(input);

  const auto it = std::find(inputs_.begin(), inputs_.end(), input);
  FX_CHECK(it != inputs_.end());
  inputs_.erase(it);
}

void Node::RemoveOutput(NodePtr output) {
  FX_CHECK(!is_meta_);
  FX_CHECK(output);
  FX_CHECK(output_ == output);

  output_ = nullptr;
}

void Node::RemoveChildInput(NodePtr child_input) {
  FX_CHECK(is_meta_);
  FX_CHECK(child_input);

  const auto it = std::find(child_inputs_.begin(), child_inputs_.end(), child_input);
  FX_CHECK(it != child_inputs_.end());
  child_inputs_.erase(it);
}

void Node::RemoveChildOutput(NodePtr child_output) {
  FX_CHECK(is_meta_);
  FX_CHECK(child_output);

  const auto it = std::find(child_outputs_.begin(), child_outputs_.end(), child_output);
  FX_CHECK(it != child_outputs_.end());
  child_outputs_.erase(it);
}

fpromise::result<void, fuchsia_audio_mixer::CreateEdgeError> Node::CreateEdge(
    GlobalTaskQueue& global_queue, NodePtr dest, NodePtr src) {
  FX_CHECK(dest);
  FX_CHECK(src);

  // Create a src child if needed.
  if (src->is_meta_) {
    if (HasOutputInChildren(src->child_outputs(), dest)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
    }
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
    if (HasInputInChildren(dest->child_inputs(), src)) {
      return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
    }
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

  if (src->output() || HasNode(dest->inputs(), src)) {
    return fpromise::error(fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
  }

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
    // Find src's output child that connects to dest or to a child of dest.
    NodePtr child;
    for (auto& c : src->child_outputs_) {
      if (c->output_ == dest || c->output_->parent() == dest) {
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

  if (!HasNode(dest->inputs_, src)) {
    return fpromise::error(fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
  }
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

}  // namespace media_audio
