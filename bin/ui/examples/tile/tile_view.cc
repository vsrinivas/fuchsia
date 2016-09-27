// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/tile/tile_view.h"

#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/services/geometry/cpp/geometry_util.h"

namespace examples {

namespace {
constexpr uint32_t kViewResourceIdBase = 100;
constexpr uint32_t kViewResourceIdSpacing = 100;

constexpr uint32_t kRootNodeId = mojo::gfx::composition::kSceneRootNodeId;
constexpr uint32_t kViewNodeIdBase = 100;
constexpr uint32_t kViewNodeIdSpacing = 100;
constexpr uint32_t kViewSceneNodeIdOffset = 1;
constexpr uint32_t kViewFallbackColorNodeIdOffset = 2;
constexpr uint32_t kViewFallbackDimLayerNodeIdOffset = 3;
constexpr uint32_t kViewFallbackDimSceneNodeIdOffset = 4;
}  // namespace

TileParams::TileParams() {}

TileParams::~TileParams() {}

TileView::TileView(
    mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
    mojo::InterfaceRequest<mojo::ui::ViewOwner> view_owner_request,
    const TileParams& params)
    : BaseView(app_connector.Pass(), view_owner_request.Pass(), "Tile"),
      params_(params) {
  ConnectViews();
}

TileView::~TileView() {}

void TileView::ConnectViews() {
  uint32_t child_key = 0;
  for (const auto& url : params_.view_urls) {
    // Start connecting to the view provider.
    mojo::ui::ViewProviderPtr provider;
    mojo::ConnectToService(app_connector(), url, mojo::GetProxy(&provider));

    FTL_LOG(INFO) << "Connecting to view: child_key=" << child_key
                  << ", url=" << url;
    mojo::ui::ViewOwnerPtr child_view_owner;
    provider->CreateView(mojo::GetProxy(&child_view_owner), nullptr);

    GetViewContainer()->AddChild(child_key, child_view_owner.Pass());
    views_.emplace(std::make_pair(
        child_key, std::unique_ptr<ViewData>(new ViewData(url, child_key))));

    child_key++;
  }
}

void TileView::OnChildAttached(uint32_t child_key,
                               mojo::ui::ViewInfoPtr child_view_info) {
  auto it = views_.find(child_key);
  FTL_DCHECK(it != views_.end());

  ViewData* view_data = it->second.get();
  view_data->view_info = child_view_info.Pass();
  Invalidate();
}

void TileView::OnChildUnavailable(uint32_t child_key) {
  auto it = views_.find(child_key);
  FTL_DCHECK(it != views_.end());
  FTL_LOG(ERROR) << "View died unexpectedly: child_key=" << child_key
                 << ", url=" << it->second->url;

  std::unique_ptr<ViewData> view_data = std::move(it->second);
  views_.erase(it);

  GetViewContainer()->RemoveChild(child_key, nullptr);
  Invalidate();
}

void TileView::OnLayout() {
  FTL_DCHECK(properties());

  // Layout all children in a row.
  if (!views_.empty()) {
    const mojo::Size& size = *properties()->view_layout->size;
    const bool vertical =
        (params_.orientation_mode == TileParams::OrientationMode::kVertical);

    uint32_t index = 0;
    uint32_t space = vertical ? size.height : size.width;
    uint32_t base = space / views_.size();
    uint32_t excess = space % views_.size();
    uint32_t offset = 0;
    for (auto it = views_.begin(); it != views_.end(); ++it, ++index) {
      ViewData* view_data = it->second.get();

      // Distribute any excess width among the leading children.
      uint32_t extent = base;
      if (excess) {
        extent++;
        excess--;
      }

      if (vertical) {
        view_data->layout_bounds.x = 0;
        view_data->layout_bounds.y = offset;
        view_data->layout_bounds.width = size.width;
        view_data->layout_bounds.height = extent;
      } else {
        view_data->layout_bounds.x = offset;
        view_data->layout_bounds.y = 0;
        view_data->layout_bounds.width = extent;
        view_data->layout_bounds.height = size.height;
      }
      offset += extent;

      auto view_properties = mojo::ui::ViewProperties::New();
      view_properties->view_layout = mojo::ui::ViewLayout::New();
      view_properties->view_layout->size = mojo::Size::New();
      view_properties->view_layout->size->width =
          view_data->layout_bounds.width;
      view_properties->view_layout->size->height =
          view_data->layout_bounds.height;

      if (view_data->view_properties.Equals(view_properties))
        continue;  // no layout work to do

      view_data->view_properties = view_properties.Clone();
      view_data->scene_version++;
      GetViewContainer()->SetChildProperties(
          it->first, view_data->scene_version, view_properties.Pass());
    }
  }
}

void TileView::OnDraw() {
  FTL_DCHECK(properties());

  // Update the scene.
  // TODO: only send the resources once, be more incremental
  auto update = mojo::gfx::composition::SceneUpdate::New();
  update->clear_resources = true;
  update->clear_nodes = true;

  // Create the root node.
  auto root_node = mojo::gfx::composition::Node::New();

  // Add the children.
  for (auto it = views_.cbegin(); it != views_.cend(); it++) {
    const ViewData& view_data = *(it->second.get());
    const uint32_t scene_resource_id =
        kViewResourceIdBase + view_data.key * kViewResourceIdSpacing;
    const uint32_t container_node_id =
        kViewNodeIdBase + view_data.key * kViewNodeIdSpacing;

    mojo::RectF extent;
    extent.width = view_data.layout_bounds.width;
    extent.height = view_data.layout_bounds.height;

    // Create a container to represent the place where the child view
    // will be presented.  The children of the container provide
    // fallback behavior in case the view is not available.
    auto container_node = mojo::gfx::composition::Node::New();
    container_node->content_clip = extent.Clone();
    container_node->content_transform = mojo::Transform::New();
    SetTranslationTransform(container_node->content_transform.get(),
                            view_data.layout_bounds.x,
                            view_data.layout_bounds.y, 0.f);

    // If we have the view, add it to the scene.
    if (view_data.view_info) {
      auto scene_resource = mojo::gfx::composition::Resource::New();
      scene_resource->set_scene(mojo::gfx::composition::SceneResource::New());
      scene_resource->get_scene()->scene_token =
          view_data.view_info->scene_token.Clone();
      update->resources.insert(scene_resource_id, scene_resource.Pass());

      const uint32_t scene_node_id = container_node_id + kViewSceneNodeIdOffset;
      auto scene_node = mojo::gfx::composition::Node::New();
      scene_node->op = mojo::gfx::composition::NodeOp::New();
      scene_node->op->set_scene(mojo::gfx::composition::SceneNodeOp::New());
      scene_node->op->get_scene()->scene_resource_id = scene_resource_id;
      if (params_.version_mode == TileParams::VersionMode::kExact)
        scene_node->op->get_scene()->scene_version = view_data.scene_version;
      update->nodes.insert(scene_node_id, scene_node.Pass());
      container_node->child_node_ids.push_back(scene_node_id);
    }

    if (params_.combinator_mode == TileParams::CombinatorMode::kPrune) {
      container_node->combinator =
          mojo::gfx::composition::Node::Combinator::PRUNE;
    } else if (params_.combinator_mode ==
               TileParams::CombinatorMode::kFallbackFlash) {
      container_node->combinator =
          mojo::gfx::composition::Node::Combinator::FALLBACK;

      const uint32_t color_node_id =
          container_node_id + kViewFallbackColorNodeIdOffset;
      auto color_node = mojo::gfx::composition::Node::New();
      color_node->op = mojo::gfx::composition::NodeOp::New();
      color_node->op->set_rect(mojo::gfx::composition::RectNodeOp::New());
      color_node->op->get_rect()->content_rect = extent.Clone();
      color_node->op->get_rect()->color = mojo::gfx::composition::Color::New();
      color_node->op->get_rect()->color->red = 255;
      color_node->op->get_rect()->color->alpha = 255;
      update->nodes.insert(color_node_id, color_node.Pass());
      container_node->child_node_ids.push_back(color_node_id);
    } else if (params_.combinator_mode ==
               TileParams::CombinatorMode::kFallbackDim) {
      container_node->combinator =
          mojo::gfx::composition::Node::Combinator::FALLBACK;

      const uint32_t dim_node_id =
          container_node_id + kViewFallbackDimLayerNodeIdOffset;
      auto dim_node = mojo::gfx::composition::Node::New();
      dim_node->combinator = mojo::gfx::composition::Node::Combinator::PRUNE;
      dim_node->op = mojo::gfx::composition::NodeOp::New();
      dim_node->op->set_layer(mojo::gfx::composition::LayerNodeOp::New());
      dim_node->op->get_layer()->layer_rect = extent.Clone();
      dim_node->op->get_layer()->blend = mojo::gfx::composition::Blend::New();
      dim_node->op->get_layer()->blend->alpha = 50;

      if (view_data.view_info) {
        const uint32_t scene_node_id =
            container_node_id + kViewFallbackDimSceneNodeIdOffset;
        auto scene_node = mojo::gfx::composition::Node::New();
        scene_node->op = mojo::gfx::composition::NodeOp::New();
        scene_node->op->set_scene(mojo::gfx::composition::SceneNodeOp::New());
        scene_node->op->get_scene()->scene_resource_id = scene_resource_id;
        update->nodes.insert(scene_node_id, scene_node.Pass());
        dim_node->child_node_ids.push_back(scene_node_id);
      }

      update->nodes.insert(dim_node_id, dim_node.Pass());
      container_node->child_node_ids.push_back(dim_node_id);
    }

    // Add the container.
    update->nodes.insert(container_node_id, container_node.Pass());
    root_node->child_node_ids.push_back(container_node_id);
  }

  // Add the root node.
  update->nodes.insert(kRootNodeId, root_node.Pass());
  scene()->Update(update.Pass());

  // Publish the scene.
  scene()->Publish(CreateSceneMetadata());
}

TileView::ViewData::ViewData(const std::string& url, uint32_t key)
    : url(url), key(key) {}

TileView::ViewData::~ViewData() {}

}  // namespace examples
