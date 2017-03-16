// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/view_host.h"

#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/geometry/cpp/geometry_util.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace modular {

constexpr uint32_t kViewResourceIdBase = 100;
constexpr uint32_t kViewResourceIdSpacing = 100;

constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr uint32_t kViewNodeIdBase = 100;
constexpr uint32_t kViewNodeIdSpacing = 100;
constexpr uint32_t kViewSceneNodeIdOffset = 1;

struct ViewData {
  explicit ViewData(uint32_t key) : key(key) {}
  const uint32_t key;
  mozart::ViewInfoPtr view_info;
  mozart::ViewPropertiesPtr view_properties;
  mozart::RectF layout_bounds;
  uint32_t scene_version = 1u;
};

ViewHost::ViewHost(mozart::ViewManagerPtr view_manager,
                   fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "ViewHost") {}

ViewHost::~ViewHost() = default;

void ViewHost::ConnectView(
    fidl::InterfaceHandle<mozart::ViewOwner> view_owner) {
  const uint32_t child_key = next_child_key_++;
  GetViewContainer()->AddChild(child_key, std::move(view_owner));
  views_.emplace(std::make_pair(
      child_key, std::unique_ptr<ViewData>(new ViewData(child_key))));
}

void ViewHost::OnChildAttached(uint32_t child_key,
                               mozart::ViewInfoPtr child_view_info) {
  auto it = views_.find(child_key);
  FTL_DCHECK(it != views_.end()) << "Invalid child_key: " << child_key;
  auto view_data = it->second.get();
  view_data->view_info = std::move(child_view_info);
  Invalidate();
}

void ViewHost::OnChildUnavailable(uint32_t child_key) {
  auto it = views_.find(child_key);
  FTL_DCHECK(it != views_.end()) << "Invalid child_key: " << child_key;
  std::unique_ptr<ViewData> view_data = std::move(it->second);
  views_.erase(it);
  GetViewContainer()->RemoveChild(child_key, nullptr);
  Invalidate();
}

void ViewHost::OnLayout() {
  FTL_DCHECK(properties());
  // Layout all children in a row.
  if (!views_.empty()) {
    const mozart::Size& size = *properties()->view_layout->size;

    uint32_t index = 0;
    uint32_t space = size.width;
    uint32_t base = space / views_.size();
    uint32_t excess = space % views_.size();
    uint32_t offset = 0;
    for (auto it = views_.begin(); it != views_.end(); ++it, ++index) {
      auto view_data = it->second.get();

      // Distribute any excess width among the leading children.
      uint32_t extent = base;
      if (excess) {
        extent++;
        excess--;
      }

      view_data->layout_bounds.x = offset;
      view_data->layout_bounds.y = 0;
      view_data->layout_bounds.width = extent;
      view_data->layout_bounds.height = size.height;
      offset += extent;

      auto view_properties = mozart::ViewProperties::New();
      view_properties->view_layout = mozart::ViewLayout::New();
      view_properties->view_layout->size = mozart::Size::New();
      view_properties->view_layout->size->width =
          view_data->layout_bounds.width;
      view_properties->view_layout->size->height =
          view_data->layout_bounds.height;
      view_properties->view_layout->inset = mozart::Inset::New();

      if (view_data->view_properties.Equals(view_properties))
        continue;  // no layout work to do

      view_data->view_properties = view_properties.Clone();
      view_data->scene_version++;
      GetViewContainer()->SetChildProperties(
          it->first, view_data->scene_version, std::move(view_properties));
    }
  }
}

void ViewHost::OnDraw() {
  FTL_DCHECK(properties());

  // Update the scene.
  auto update = mozart::SceneUpdate::New();
  update->clear_resources = true;
  update->clear_nodes = true;

  // Create the root node.
  auto root_node = mozart::Node::New();

  // Add the children.
  for (auto it = views_.cbegin(); it != views_.cend(); it++) {
    const ViewData& view_data = *(it->second.get());
    const uint32_t scene_resource_id =
        kViewResourceIdBase + view_data.key * kViewResourceIdSpacing;
    const uint32_t container_node_id =
        kViewNodeIdBase + view_data.key * kViewNodeIdSpacing;

    mozart::RectF extent;
    extent.width = view_data.layout_bounds.width;
    extent.height = view_data.layout_bounds.height;

    // Create a container to represent the place where the child view
    // will be presented.  The children of the container provide
    // fallback behavior in case the view is not available.
    auto container_node = mozart::Node::New();
    container_node->content_clip = extent.Clone();
    container_node->content_transform = mozart::Transform::New();
    mozart::SetTranslationTransform(container_node->content_transform.get(),
                                    view_data.layout_bounds.x,
                                    view_data.layout_bounds.y, 0.f);

    // If we have the view, add it to the scene.
    if (view_data.view_info) {
      auto scene_resource = mozart::Resource::New();
      scene_resource->set_scene(mozart::SceneResource::New());
      scene_resource->get_scene()->scene_token =
          view_data.view_info->scene_token.Clone();
      update->resources.insert(scene_resource_id, std::move(scene_resource));

      const uint32_t scene_node_id = container_node_id + kViewSceneNodeIdOffset;
      auto scene_node = mozart::Node::New();
      scene_node->op = mozart::NodeOp::New();
      scene_node->op->set_scene(mozart::SceneNodeOp::New());
      scene_node->op->get_scene()->scene_resource_id = scene_resource_id;
      update->nodes.insert(scene_node_id, std::move(scene_node));
      container_node->child_node_ids.push_back(scene_node_id);
    }

    // Add the container.
    update->nodes.insert(container_node_id, std::move(container_node));
    root_node->child_node_ids.push_back(container_node_id);
  }

  // Add the root node.
  update->nodes.insert(kRootNodeId, std::move(root_node));
  scene()->Update(std::move(update));

  // Publish the scene.
  scene()->Publish(CreateSceneMetadata());
}

}  // namespace modular
