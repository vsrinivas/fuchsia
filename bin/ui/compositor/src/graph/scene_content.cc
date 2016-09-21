// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/graph/scene_content.h"

#include <ostream>

#include "apps/compositor/src/graph/scene_def.h"
#include "lib/ftl/logging.h"

namespace compositor {

SceneContent::SceneContent(const SceneLabel& label,
                           uint32_t version,
                           int64_t presentation_time,
                           size_t max_resources,
                           size_t max_nodes)
    : label_(label),
      version_(version),
      presentation_time_(presentation_time),
      resources_(max_resources),
      nodes_(max_nodes) {}

SceneContent::~SceneContent() {}

bool SceneContent::MatchesVersion(uint32_t requested_version) const {
  return requested_version == mojo::gfx::composition::kSceneVersionNone ||
         requested_version == version_ ||
         version_ == mojo::gfx::composition::kSceneVersionNone;
}

void SceneContent::Paint(const Snapshot* snapshot, SkCanvas* canvas) const {
  const Node* root = GetRootNodeIfExists();
  if (root)
    root->Paint(this, snapshot, canvas);
}

bool SceneContent::HitTest(
    const Snapshot* snapshot,
    const SkPoint& scene_point,
    const SkMatrix44& global_to_scene_transform,
    mojo::gfx::composition::SceneHitPtr* out_scene_hit) const {
  FTL_DCHECK(snapshot);
  FTL_DCHECK(out_scene_hit);

  const Node* root = GetRootNodeIfExists();
  if (!root)
    return false;

  mojo::Array<mojo::gfx::composition::HitPtr> hits;
  bool opaque = root->HitTest(this, snapshot, scene_point,
                              global_to_scene_transform, &hits);
  if (hits.size()) {
    auto scene_hit = mojo::gfx::composition::SceneHit::New();
    scene_hit->scene_token = mojo::gfx::composition::SceneToken::New();
    scene_hit->scene_token->value = label_.token();
    scene_hit->scene_version = version_;
    scene_hit->hits = hits.Pass();
    *out_scene_hit = scene_hit.Pass();
  }
  return opaque;
}

const Resource* SceneContent::GetResource(uint32_t resource_id,
                                          Resource::Type resource_type) const {
  auto it = resources_.find(resource_id);
  FTL_DCHECK(it != resources_.end());
  FTL_DCHECK(it->second->type() == resource_type);
  return it->second.get();
}

const Node* SceneContent::GetNode(uint32_t node_id) const {
  auto it = nodes_.find(node_id);
  FTL_DCHECK(it != nodes_.end());
  return it->second.get();
}

const Node* SceneContent::GetRootNodeIfExists() const {
  auto it = nodes_.find(mojo::gfx::composition::kSceneRootNodeId);
  return it != nodes_.end() ? it->second.get() : nullptr;
}

SceneContentBuilder::SceneContentBuilder(const SceneLabel& label,
                                         uint32_t version,
                                         int64_t presentation_time,
                                         size_t max_resources,
                                         size_t max_nodes,
                                         std::ostream& err)
    : content_(new SceneContent(label,
                                version,
                                presentation_time,
                                max_resources,
                                max_nodes)),
      err_(err) {}

SceneContentBuilder::~SceneContentBuilder() {}

const Resource* SceneContentBuilder::RequireResource(
    uint32_t resource_id,
    Resource::Type resource_type,
    uint32_t referrer_node_id) {
  FTL_DCHECK(content_);

  auto it = content_->resources_.find(resource_id);
  if (it != content_->resources_.end())
    return it->second.get();

  const Resource* resource = FindResource(resource_id);
  if (!resource) {
    err_ << "Missing resource " << resource_id << " referenced from node "
         << content_->FormattedLabelForNode(referrer_node_id) << std::endl;
    return nullptr;
  }

  if (resource->type() != resource_type) {
    err_ << "Resource " << resource_id << " referenced from node "
         << content_->FormattedLabelForNode(referrer_node_id)
         << " has incorrect type for its intended usage" << std::endl;
    return nullptr;
  }

  content_->resources_.emplace(std::make_pair(resource_id, resource));
  return resource;
}

const Node* SceneContentBuilder::RequireNode(uint32_t node_id,
                                             uint32_t referrer_node_id) {
  FTL_DCHECK(content_);

  auto it = content_->nodes_.find(node_id);
  if (it != content_->nodes_.end()) {
    if (it->second)
      return it->second.get();
    err_ << "Cycle detected at node " << node_id << " referenced from node "
         << content_->FormattedLabelForNode(referrer_node_id) << std::endl;
    return nullptr;
  }

  const Node* node = FindNode(node_id);
  if (!node) {
    err_ << "Missing node " << node_id << " referenced from node "
         << content_->FormattedLabelForNode(referrer_node_id) << std::endl;
    return nullptr;
  }

  return AddNode(node) ? node : nullptr;
}

bool SceneContentBuilder::AddNode(const Node* node) {
  FTL_DCHECK(content_);
  FTL_DCHECK(node);

  // Reserve a spot in the table to mark the node recording in progress.
  FTL_DCHECK(content_->nodes_.size() < content_->nodes_.bucket_count());
  auto storage = content_->nodes_.emplace(node->node_id(), nullptr);
  FTL_DCHECK(storage.second);

  // Record the node's content.
  // This performs a depth first search of the node.  If it succeeds, we
  // will know that this part of the graph has no cycles.  Note that this
  // function may recurse back into |AddNode| to add additional nodes.
  if (!node->RecordContent(this))
    return false;

  // Store the node in the table.
  // It is safe to use the iterator returned by emplace even though additional
  // nodes may have been added since the map's bucket count was initialized
  // at creation time to the total number of nodes so it should never be
  // rehashed during this traversal.
  storage.first->second = ftl::Ref(node);
  return true;
}

ftl::RefPtr<const SceneContent> SceneContentBuilder::Build() {
  FTL_DCHECK(content_);

  const Node* root = FindNode(mojo::gfx::composition::kSceneRootNodeId);
  return !root || AddNode(root) ? std::move(content_) : nullptr;
}

}  // namespace compositor
