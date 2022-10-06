// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/node.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "src/media/audio/lib/clock/clock.h"
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
  FX_CHECK(source->type() != Node::Type::kMeta);
  return std::find_if(children.cbegin(), children.cend(), [&source](const NodePtr& child) {
           FX_CHECK(child->type() != Node::Type::kMeta);
           return HasNode(child->sources(), source);
         }) != children.cend();
}

bool HasDestInChildren(const std::vector<NodePtr>& children, const NodePtr& dest) {
  FX_CHECK(dest);
  return std::find_if(children.cbegin(), children.cend(), [&dest](const NodePtr& child) {
           FX_CHECK(child->type() != Node::Type::kMeta);
           return dest->type() == Node::Type::kMeta ? HasNode(dest->child_sources(), child->dest())
                                                    : dest == child->dest();
         }) != children.cend();
}

}  // namespace

Node::Node(Type type, std::string_view name, std::shared_ptr<Clock> reference_clock,
           PipelineDirection pipeline_direction, PipelineStagePtr pipeline_stage, NodePtr parent)
    : type_(type),
      name_(name),
      reference_clock_(std::move(reference_clock)),
      pipeline_direction_(pipeline_direction),
      pipeline_stage_(std::move(pipeline_stage)),
      parent_(std::move(parent)) {
  if (parent_) {
    FX_CHECK(parent_->type_ == Type::kMeta);
  }
  if (type_ == Type::kMeta) {
    FX_CHECK(!parent_);          // nested meta nodes are not allowed
    FX_CHECK(!pipeline_stage_);  // meta nodes cannot own PipelineStages
  } else {
    FX_CHECK(pipeline_stage_);  // each ordinary node own a PipelineStage
    FX_CHECK(pipeline_stage_->reference_clock() == reference_clock_);
  }
}

fpromise::result<void, fuchsia_audio_mixer::CreateEdgeError> Node::CreateEdge(
    GlobalTaskQueue& global_queue, GraphDetachedThreadPtr detached_thread, NodePtr source,
    NodePtr dest) {
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
  if (source->type() == Type::kMeta) {
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
  if (dest->type() == Type::kMeta) {
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
  auto stages_to_move =
      MoveNodeToThread(*source, /*new_thread=*/dest->thread(), /*expected_thread=*/detached_thread);

  // Update downstream consumer counts. Do this after moving to `dest->threaD()` so updates to
  // `source` get batched into the `dest->thread()` async task.
  auto stages_to_update_downstream_consumers = RecomputeMaxDownstreamConsumers(*source);

  // Update the PipelineStages asynchronously.
  // Fist apply updates that must happen on dest's thread, which includes connecting source -> dest.
  global_queue.Push(dest->thread()->id(),
                    [dest_stage = dest->pipeline_stage(),         //
                     source_stage = source->pipeline_stage(),     //
                     stages_to_move = std::move(stages_to_move),  //
                     stages_to_update_downstream_consumers =
                         std::move(stages_to_update_downstream_consumers[dest->thread()->id()]),  //
                     new_thread = dest->thread()->pipeline_thread(),                              //
                     old_thread = detached_thread->pipeline_thread()]() {
                      // Before we acquire a checker, verify the dest_stage has the expected thread.
                      FX_CHECK(dest_stage->thread() == new_thread)
                          << dest_stage->thread()->name() << " != " << new_thread->name();

                      // Move all stages to `new_thread` before creating the source -> dest link.
                      for (auto& stage : stages_to_move) {
                        FX_CHECK(stage->thread() == old_thread)
                            << stage->thread()->name() << " != " << old_thread->name();
                        stage->set_thread(new_thread);
                      }

                      // Update downstream consumer counts.
                      for (auto& [stage, count] : stages_to_update_downstream_consumers) {
                        ScopedThreadChecker checker(stage->thread()->checker());
                        stage->set_max_downstream_consumers(count);
                      }

                      ScopedThreadChecker checker(dest_stage->thread()->checker());
                      // TODO(fxbug.dev/87651): Pass in `gain_ids`.
                      dest_stage->AddSource(source_stage, /*options=*/{});
                    });

  // Apply updates that must happen on other threads.
  for (auto& [thread_id, changes] : stages_to_update_downstream_consumers) {
    if (thread_id == dest->thread()->id()) {
      continue;
    }
    global_queue.Push(thread_id,
                      [updates = std::move(stages_to_update_downstream_consumers[thread_id])]() {
                        for (auto& [stage, count] : updates) {
                          ScopedThreadChecker checker(stage->thread()->checker());
                          stage->set_max_downstream_consumers(count);
                        }
                      });
  }

  return fpromise::ok();
}

fpromise::result<void, fuchsia_audio_mixer::DeleteEdgeError> Node::DeleteEdge(
    GlobalTaskQueue& global_queue, GraphDetachedThreadPtr detached_thread, NodePtr source,
    NodePtr dest) {
  FX_CHECK(source);
  FX_CHECK(dest);

  NodePtr source_parent;
  NodePtr dest_parent;

  if (source->type() == Type::kMeta) {
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

  if (dest->type() == Type::kMeta) {
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

  // Update downstream consumer counts. Do this before moving to `detached_thread` so updates to
  // `source` get batched into the `dest->thread()` async task.
  auto stages_to_update_downstream_consumers = RecomputeMaxDownstreamConsumers(*source);

  // Since the source was previously connected to dest, it must be owned by the same thread as dest.
  // Since the source is now disconnected, it moves to the detached thread.
  auto stages_to_move =
      MoveNodeToThread(*source, /*new_thread=*/detached_thread, /*expected_thread=*/dest->thread());

  // The PipelineStages are updated asynchronously.
  global_queue.Push(dest->thread()->id(),
                    [dest_stage = dest->pipeline_stage(),         //
                     source_stage = source->pipeline_stage(),     //
                     stages_to_move = std::move(stages_to_move),  //
                     stages_to_update_downstream_consumers =
                         std::move(stages_to_update_downstream_consumers[dest->thread()->id()]),  //
                     new_thread = detached_thread->pipeline_thread(),                             //
                     old_thread = dest->thread()->pipeline_thread()]() {
                      // Before we acquire a checker, verify the dest_stage has the expected thread.
                      FX_CHECK(dest_stage->thread() == old_thread)
                          << dest_stage->thread()->name() << " != " << new_thread->name();

                      ScopedThreadChecker checker(dest_stage->thread()->checker());
                      dest_stage->RemoveSource(source_stage);

                      // Move all disconnected stages to the detached thread.
                      for (auto& stage : stages_to_move) {
                        FX_CHECK(stage->thread() == old_thread)
                            << stage->thread()->name() << " != " << old_thread->name();
                        stage->set_thread(new_thread);
                      }

                      // Update downstream consumer counts.
                      for (auto& [stage, count] : stages_to_update_downstream_consumers) {
                        ScopedThreadChecker checker(stage->thread()->checker());
                        stage->set_max_downstream_consumers(count);
                      }
                    });

  // Apply updates that must happen on other threads.
  for (auto& [thread_id, changes] : stages_to_update_downstream_consumers) {
    if (thread_id == dest->thread()->id()) {
      continue;
    }
    global_queue.Push(thread_id,
                      [updates = std::move(stages_to_update_downstream_consumers[thread_id])]() {
                        for (auto& [stage, count] : updates) {
                          ScopedThreadChecker checker(stage->thread()->checker());
                          stage->set_max_downstream_consumers(count);
                        }
                      });
  }

  return fpromise::ok();
}

void Node::Destroy(GlobalTaskQueue& global_queue, GraphDetachedThreadPtr detached_thread,
                   NodePtr node) {
  // We call DeleteEdge for each existing edge. Since these edges exist, DeleteEdge cannot fail.
  auto delete_edge = [&global_queue, &detached_thread](NodePtr source, NodePtr dest) {
    // When deleting an edge A->B, if A is a dynamically-created child node, then we should delete
    // the edge [A.parent]->B to ensure we cleanup state in A.parent.
    auto lift_source_to_parent = [](NodePtr a) {
      if (a->type() == Type::kMeta || !a->parent()) {
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
      if (a->type() == Type::kMeta || !a->parent()) {
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

  if (node->type() != Type::kMeta) {
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
  FX_CHECK(type_ != Type::kMeta);
  return sources_;
}

NodePtr Node::dest() const {
  FX_CHECK(type_ != Type::kMeta);
  return dest_;
}

const std::vector<NodePtr>& Node::child_sources() const {
  FX_CHECK(type_ == Type::kMeta);
  return child_sources_;
}

const std::vector<NodePtr>& Node::child_dests() const {
  FX_CHECK(type_ == Type::kMeta);
  return child_dests_;
}

NodePtr Node::parent() const {
  FX_CHECK(type_ != Type::kMeta);
  return parent_;
}

PipelineStagePtr Node::pipeline_stage() const {
  FX_CHECK(type_ != Type::kMeta);
  return pipeline_stage_;
}

std::shared_ptr<GraphThread> Node::thread() const {
  FX_CHECK(type_ != Type::kMeta);
  return thread_;
}

void Node::set_thread(std::shared_ptr<GraphThread> t) {
  FX_CHECK(type_ != Type::kMeta);
  thread_ = t;
}

int64_t Node::max_downstream_consumers() const {
  FX_CHECK(type_ != Type::kMeta);
  return max_downstream_consumers_;
}

void Node::set_max_downstream_consumers(int64_t max) {
  FX_CHECK(type_ != Type::kMeta);
  max_downstream_consumers_ = max;
}

void Node::SetBuiltInChildren(std::vector<NodePtr> child_sources,
                              std::vector<NodePtr> child_dests) {
  FX_CHECK(type_ == Type::kMeta);
  FX_CHECK(child_sources_.empty());
  FX_CHECK(child_dests_.empty());

  child_sources_ = std::move(child_sources);
  child_dests_ = std::move(child_dests);
  built_in_children_ = true;
}

void Node::AddSource(NodePtr source) {
  FX_CHECK(type_ != Type::kMeta);
  FX_CHECK(source);
  sources_.push_back(std::move(source));
}

void Node::SetDest(NodePtr dest) {
  FX_CHECK(type_ != Type::kMeta);
  FX_CHECK(dest);
  dest_ = std::move(dest);
}

void Node::AddChildSource(NodePtr child_source) {
  FX_CHECK(type_ == Type::kMeta);
  FX_CHECK(!built_in_children_);
  FX_CHECK(child_source);
  child_sources_.push_back(std::move(child_source));
}

void Node::AddChildDest(NodePtr child_dest) {
  FX_CHECK(type_ == Type::kMeta);
  FX_CHECK(!built_in_children_);
  FX_CHECK(child_dest);
  child_dests_.push_back(std::move(child_dest));
}

void Node::RemoveSource(NodePtr source) {
  FX_CHECK(type_ != Type::kMeta);
  FX_CHECK(source);

  const auto it = std::find(sources_.begin(), sources_.end(), source);
  FX_CHECK(it != sources_.end());
  sources_.erase(it);
}

void Node::RemoveDest(NodePtr dest) {
  FX_CHECK(type_ != Type::kMeta);
  FX_CHECK(dest);
  FX_CHECK(dest_ == dest);

  dest_ = nullptr;
}

void Node::RemoveChildSource(NodePtr child_source) {
  FX_CHECK(type_ == Type::kMeta);
  FX_CHECK(!built_in_children_);
  FX_CHECK(child_source);

  const auto it = std::find(child_sources_.begin(), child_sources_.end(), child_source);
  FX_CHECK(it != child_sources_.end());
  child_sources_.erase(it);
  DestroyChildSource(child_source);
}

void Node::RemoveChildDest(NodePtr child_dest) {
  FX_CHECK(type_ == Type::kMeta);
  FX_CHECK(!built_in_children_);
  FX_CHECK(child_dest);

  const auto it = std::find(child_dests_.begin(), child_dests_.end(), child_dest);
  FX_CHECK(it != child_dests_.end());
  child_dests_.erase(it);
  DestroyChildDest(child_dest);
}

}  // namespace media_audio
