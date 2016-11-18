// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/graph/scene_def.h"

#include <ostream>

#include "apps/mozart/lib/skia/type_converters.h"
#include "apps/mozart/services/composition/cpp/formatting.h"
#include "apps/mozart/src/compositor/graph/scene_content.h"
#include "apps/mozart/src/compositor/graph/transform_pair.h"
#include "apps/mozart/src/compositor/graph/universe.h"
#include "apps/mozart/src/compositor/render/render_image.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"

namespace compositor {

SceneDef::SceneDef(const SceneLabel& label) : label_(label) {}

SceneDef::~SceneDef() {}

void SceneDef::EnqueueUpdate(mozart::SceneUpdatePtr update) {
  FTL_DCHECK(update);
  pending_updates_.push_back(std::move(update));
}

void SceneDef::EnqueuePublish(mozart::SceneMetadataPtr metadata) {
  FTL_DCHECK(metadata);
  pending_publications_.emplace_back(new Publication(std::move(metadata)));
  pending_updates_.swap(pending_publications_.back()->updates);
}

SceneDef::Disposition SceneDef::Present(
    ftl::TimePoint presentation_time,
    Universe* universe,
    const SceneResolver& resolver,
    const SceneUnavailableSender& unavailable_sender,
    std::ostream& err) {
  // Walk backwards through the pending publications to find the index
  // just beyond the last one which is due to be presented at or before the
  // presentation time.
  size_t end = pending_publications_.size();
  for (;;) {
    if (!end)
      return Disposition::kUnchanged;
    if (pending_publications_[end - 1]->is_due(presentation_time))
      break;  // found last presentable publication
    end--;
  }

  // TODO(jeffbrown): Should we publish every individual update to the
  // universe or is it good enough to only capture the most recent
  // accumulated updates at presentation time as we do here?

  // Apply all updates sequentially up to this point.
  uint32_t version = pending_publications_[end - 1]->metadata->version;
  for (size_t index = 0; index < end; ++index) {
    for (auto& update : pending_publications_[index]->updates) {
      if (!ApplyUpdate(std::move(update), universe, resolver,
                       unavailable_sender, err))
        return Disposition::kFailed;
    }
  }

  // Dequeue the publications we processed.
  pending_publications_.erase(pending_publications_.begin(),
                              pending_publications_.begin() + end);

  // Rebuild the scene content, collecting all reachable nodes and resources
  // and verifying that everything is correctly linked.
  Collector collector(this, version, presentation_time, err);
  ftl::RefPtr<const SceneContent> content = collector.Build();
  if (!content)
    return Disposition::kFailed;

  universe->PresentScene(content);
  return Disposition::kSucceeded;
}

bool SceneDef::ApplyUpdate(mozart::SceneUpdatePtr update,
                           Universe* universe,
                           const SceneResolver& resolver,
                           const SceneUnavailableSender& unavailable_sender,
                           std::ostream& err) {
  FTL_DCHECK(update);

  // TODO(jeffbrown): We may be able to reuse some content from previous
  // versions even when the client removes and recreates resources or nodes.
  // To reduce unnecessary churn, consider keeping track of items which have
  // been removed or are being replaced then checking to see whether they
  // really changed.

  // Update resources.
  if (update->clear_resources) {
    resources_.clear();
  }
  for (auto it = update->resources.begin(); it != update->resources.end();
       ++it) {
    uint32_t resource_id = it.GetKey();
    mozart::ResourcePtr& resource_decl = it.GetValue();
    if (resource_decl) {
      ftl::RefPtr<const Resource> resource =
          CreateResource(resource_id, std::move(resource_decl), universe,
                         resolver, unavailable_sender, err);
      if (!resource)
        return false;
      resources_[resource_id] = std::move(resource);
    } else {
      resources_.erase(resource_id);
    }
  }

  // Update nodes.
  if (update->clear_nodes) {
    nodes_.clear();
  }
  for (auto it = update->nodes.begin(); it != update->nodes.end(); ++it) {
    uint32_t node_id = it.GetKey();
    mozart::NodePtr& node_decl = it.GetValue();
    if (node_decl) {
      ftl::RefPtr<const Node> node =
          CreateNode(node_id, std::move(node_decl), err);
      if (!node)
        return false;
      nodes_[node_id] = std::move(node);
    } else {
      nodes_.erase(node_id);
    }
  }
  return true;
}

void SceneDef::NotifySceneUnavailable(
    const mozart::SceneToken& scene_token,
    const SceneUnavailableSender& unavailable_sender) {
  for (auto& pair : resources_) {
    if (pair.second->type() == Resource::Type::kScene) {
      auto scene_resource =
          static_cast<const SceneResource*>(pair.second.get());
      if (scene_resource->scene_token().value == scene_token.value)
        unavailable_sender(pair.first);
    }
  }
}

ftl::RefPtr<const Resource> SceneDef::CreateResource(
    uint32_t resource_id,
    mozart::ResourcePtr resource_decl,
    Universe* universe,
    const SceneResolver& resolver,
    const SceneUnavailableSender& unavailable_sender,
    std::ostream& err) {
  FTL_DCHECK(resource_decl);

  if (resource_decl->is_scene()) {
    auto& scene_resource_decl = resource_decl->get_scene();
    FTL_DCHECK(scene_resource_decl->scene_token);

    const mozart::SceneToken& scene_token = *scene_resource_decl->scene_token;
    if (!resolver(scene_token))
      unavailable_sender(resource_id);
    return ftl::MakeRefCounted<SceneResource>(scene_token);
  }

  if (resource_decl->is_image()) {
    auto& image_resource_decl = resource_decl->get_image();
    FTL_DCHECK(image_resource_decl->image);

    auto& image_decl = image_resource_decl->image;
    ftl::RefPtr<RenderImage> image = RenderImage::CreateFromImage(
        std::move(image_decl), universe->buffer_consumer());
    if (!image) {
      err << "ImageResource is invalid: resource_id=" << resource_id;
      return nullptr;
    }
    return ftl::MakeRefCounted<ImageResource>(std::move(image));
  }

  err << "Unsupported resource type: resource_id=" << resource_id;
  return nullptr;
}

ftl::RefPtr<const Node> SceneDef::CreateNode(uint32_t node_id,
                                             mozart::NodePtr node_decl,
                                             std::ostream& err) {
  FTL_DCHECK(node_decl);

  std::unique_ptr<TransformPair> content_transform;
  if (node_decl->content_transform) {
    content_transform.reset(
        new TransformPair(node_decl->content_transform.To<SkMatrix44>()));
  }
  mozart::RectFPtr content_clip = std::move(node_decl->content_clip);
  mozart::HitTestBehaviorPtr hit_test_behavior =
      std::move(node_decl->hit_test_behavior);
  const mozart::Node::Combinator combinator = node_decl->combinator;
  const std::vector<uint32_t>& child_node_ids =
      node_decl->child_node_ids.storage();

  if (!node_decl->op) {
    return ftl::MakeRefCounted<Node>(
        node_id, std::move(content_transform), std::move(content_clip),
        std::move(hit_test_behavior), combinator, child_node_ids);
  }

  if (node_decl->op->is_rect()) {
    auto& rect_node_decl = node_decl->op->get_rect();
    FTL_DCHECK(rect_node_decl->content_rect);
    FTL_DCHECK(rect_node_decl->color);

    const mozart::RectF& content_rect = *rect_node_decl->content_rect;
    const mozart::Color& color = *rect_node_decl->color;
    return ftl::MakeRefCounted<RectNode>(
        node_id, std::move(content_transform), std::move(content_clip),
        std::move(hit_test_behavior), combinator, child_node_ids, content_rect,
        color);
  }

  if (node_decl->op->is_image()) {
    auto& image_node_decl = node_decl->op->get_image();
    FTL_DCHECK(image_node_decl->content_rect);

    const mozart::RectF& content_rect = *image_node_decl->content_rect;
    mozart::RectFPtr image_rect = std::move(image_node_decl->image_rect);
    const uint32_t image_resource_id = image_node_decl->image_resource_id;
    mozart::BlendPtr blend = std::move(image_node_decl->blend);
    return ftl::MakeRefCounted<ImageNode>(
        node_id, std::move(content_transform), std::move(content_clip),
        std::move(hit_test_behavior), combinator, child_node_ids, content_rect,
        std::move(image_rect), image_resource_id, std::move(blend));
  }

  if (node_decl->op->is_scene()) {
    auto& scene_node_decl = node_decl->op->get_scene();

    const uint32_t scene_resource_id = scene_node_decl->scene_resource_id;
    const uint32_t scene_version = scene_node_decl->scene_version;
    return ftl::MakeRefCounted<SceneNode>(
        node_id, std::move(content_transform), std::move(content_clip),
        std::move(hit_test_behavior), combinator, child_node_ids,
        scene_resource_id, scene_version);
  }

  if (node_decl->op->is_layer()) {
    auto& layer_node_decl = node_decl->op->get_layer();
    FTL_DCHECK(layer_node_decl->layer_rect);

    const mozart::RectF& layer_rect = *layer_node_decl->layer_rect;
    mozart::BlendPtr blend = std::move(layer_node_decl->blend);
    return ftl::MakeRefCounted<LayerNode>(
        node_id, std::move(content_transform), std::move(content_clip),
        std::move(hit_test_behavior), combinator, child_node_ids, layer_rect,
        std::move(blend));
  }

  err << "Unsupported node op type: node_id=" << node_id
      << ", node_op=" << node_decl->op;
  return nullptr;
}

SceneDef::Collector::Collector(const SceneDef* scene,
                               uint32_t version,
                               ftl::TimePoint presentation_time,
                               std::ostream& err)
    : SceneContentBuilder(scene->label_,
                          version,
                          presentation_time,
                          scene->resources_.size(),
                          scene->nodes_.size(),
                          err),
      scene_(scene) {
  FTL_DCHECK(scene_);
}

SceneDef::Collector::~Collector() {}

const Node* SceneDef::Collector::FindNode(uint32_t node_id) const {
  auto it = scene_->nodes_.find(node_id);
  return it != scene_->nodes_.end() ? it->second.get() : nullptr;
}

const Resource* SceneDef::Collector::FindResource(uint32_t resource_id) const {
  auto it = scene_->resources_.find(resource_id);
  return it != scene_->resources_.end() ? it->second.get() : nullptr;
}

SceneDef::Publication::Publication(mozart::SceneMetadataPtr metadata)
    : metadata(std::move(metadata)) {
  FTL_DCHECK(this->metadata);
}

SceneDef::Publication::~Publication() {}

}  // namespace compositor
