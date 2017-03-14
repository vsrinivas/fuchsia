// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/tile/tile_view.h"

#include "application/lib/app/connect.h"
#include "apps/mozart/services/geometry/cpp/geometry_util.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/ftl/logging.h"

namespace examples {

namespace {
constexpr uint32_t kViewResourceIdBase = 100;
constexpr uint32_t kViewResourceIdSpacing = 100;

constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr uint32_t kViewNodeIdBase = 100;
constexpr uint32_t kViewNodeIdSpacing = 100;
constexpr uint32_t kViewSceneNodeIdOffset = 1;
constexpr uint32_t kViewFallbackColorNodeIdOffset = 2;
constexpr uint32_t kViewFallbackDimLayerNodeIdOffset = 3;
constexpr uint32_t kViewFallbackDimSceneNodeIdOffset = 4;
}  // namespace

TileView::TileView(mozart::ViewManagerPtr view_manager,
                   fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                   app::ApplicationContext* application_context,
                   const TileParams& params)
    : BaseView(std::move(view_manager), std::move(view_owner_request), "Tile"),
      env_host_binding_(this),
      application_context_(application_context),
      params_(params) {
  CreateNestedEnvironment();
  ConnectViews();
}

TileView::~TileView() {}

// Adds a view as a child.
void TileView::PresentHelper(
    fidl::InterfaceHandle<mozart::ViewOwner> child_view_owner,
    const std::string& url,
    app::ApplicationControllerPtr app_controller) {
  child_key_++;
  GetViewContainer()->AddChild(child_key_, std::move(child_view_owner));

  views_.emplace(std::make_pair(
      child_key_, std::unique_ptr<ViewData>(new ViewData(
                      url, child_key_, std::move(app_controller)))));
}

// Adds a view as a child.
void TileView::Present(
    fidl::InterfaceHandle<mozart::ViewOwner> child_view_owner) {
  const std::string empty_url;
  PresentHelper(std::move(child_view_owner), empty_url, nullptr);
}

// Launches initial list of views, passed as command line parameters.
void TileView::ConnectViews() {
  for (const auto& url : params_.view_urls) {
    app::ServiceProviderPtr services;
    app::ApplicationControllerPtr controller;

    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = url;
    launch_info->services = services.NewRequest();

    // |env_launcher_| launches the app with our nested environment.
    env_launcher_->CreateApplication(std::move(launch_info),
                                     controller.NewRequest());

    // Get the view provider back from the launched app.
    auto view_provider =
        app::ConnectToService<mozart::ViewProvider>(services.get());

    fidl::InterfaceHandle<mozart::ViewOwner> child_view_owner;
    view_provider->CreateView(child_view_owner.NewRequest(), nullptr);

    // Add the view, which increments child_key_.
    PresentHelper(std::move(child_view_owner), url, std::move(controller));
  }
}

// Required method for |ApplicationEnvironmentHost|
void TileView::GetApplicationEnvironmentServices(
    fidl::InterfaceRequest<app::ServiceProvider> environment_services) {
  env_services_.AddBinding(std::move(environment_services));
}

// Set up environment with a |Presenter| service. We launch apps with this
// environment
void TileView::CreateNestedEnvironment() {
  app::ApplicationEnvironmentHostPtr env_host;
  env_host_binding_.Bind(env_host.NewRequest());
  application_context_->environment()->CreateNestedEnvironment(
      std::move(env_host), env_.NewRequest(), env_controller_.NewRequest(),
      "tile");
  env_->GetApplicationLauncher(env_launcher_.NewRequest());

  // Add a binding for the presenter service
  env_services_.AddService<mozart::Presenter>(
      [this](fidl::InterfaceRequest<mozart::Presenter> request) {
        presenter_bindings_.AddBinding(this, std::move(request));
      });

  env_services_.SetDefaultServiceConnector(
      [this](std::string service_name, mx::channel channel) {
        application_context_->environment_services()->ConnectToService(
            service_name, std::move(channel));
      });
}

void TileView::OnChildAttached(uint32_t child_key,
                               mozart::ViewInfoPtr child_view_info) {
  auto it = views_.find(child_key);
  FTL_DCHECK(it != views_.end());

  ViewData* view_data = it->second.get();
  view_data->view_info = std::move(child_view_info);
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
    const mozart::Size& size = *properties()->view_layout->size;
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

      auto view_properties = mozart::ViewProperties::New();
      view_properties->view_layout = mozart::ViewLayout::New();
      view_properties->view_layout->size = mozart::Size::New();
      view_properties->view_layout->size->width =
          view_data->layout_bounds.width;
      view_properties->view_layout->size->height =
          view_data->layout_bounds.height;
      view_properties->view_layout->inset = mozart::Inset::New();
      view_properties->view_layout->inset->top = 0;
      view_properties->view_layout->inset->right = 0;
      view_properties->view_layout->inset->bottom = 0;
      view_properties->view_layout->inset->left = 0;

      if (view_data->view_properties.Equals(view_properties))
        continue;  // no layout work to do

      view_data->view_properties = view_properties.Clone();
      view_data->scene_version++;
      GetViewContainer()->SetChildProperties(
          it->first, view_data->scene_version, std::move(view_properties));
    }
  }
}

void TileView::OnDraw() {
  FTL_DCHECK(properties());

  // Update the scene.
  // TODO: only send the resources once, be more incremental
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
    SetTranslationTransform(container_node->content_transform.get(),
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
      if (params_.version_mode == TileParams::VersionMode::kExact)
        scene_node->op->get_scene()->scene_version = view_data.scene_version;
      update->nodes.insert(scene_node_id, std::move(scene_node));
      container_node->child_node_ids.push_back(scene_node_id);
    }

    if (params_.combinator_mode == TileParams::CombinatorMode::kPrune) {
      container_node->combinator = mozart::Node::Combinator::PRUNE;
    } else if (params_.combinator_mode ==
               TileParams::CombinatorMode::kFallbackFlash) {
      container_node->combinator = mozart::Node::Combinator::FALLBACK;

      const uint32_t color_node_id =
          container_node_id + kViewFallbackColorNodeIdOffset;
      auto color_node = mozart::Node::New();
      color_node->op = mozart::NodeOp::New();
      color_node->op->set_rect(mozart::RectNodeOp::New());
      color_node->op->get_rect()->content_rect = extent.Clone();
      color_node->op->get_rect()->color = mozart::Color::New();
      color_node->op->get_rect()->color->red = 255;
      color_node->op->get_rect()->color->alpha = 255;
      update->nodes.insert(color_node_id, std::move(color_node));
      container_node->child_node_ids.push_back(color_node_id);
    } else if (params_.combinator_mode ==
               TileParams::CombinatorMode::kFallbackDim) {
      container_node->combinator = mozart::Node::Combinator::FALLBACK;

      const uint32_t dim_node_id =
          container_node_id + kViewFallbackDimLayerNodeIdOffset;
      auto dim_node = mozart::Node::New();
      dim_node->combinator = mozart::Node::Combinator::PRUNE;
      dim_node->op = mozart::NodeOp::New();
      dim_node->op->set_layer(mozart::LayerNodeOp::New());
      dim_node->op->get_layer()->layer_rect = extent.Clone();
      dim_node->op->get_layer()->blend = mozart::Blend::New();
      dim_node->op->get_layer()->blend->alpha = 50;

      if (view_data.view_info) {
        const uint32_t scene_node_id =
            container_node_id + kViewFallbackDimSceneNodeIdOffset;
        auto scene_node = mozart::Node::New();
        scene_node->op = mozart::NodeOp::New();
        scene_node->op->set_scene(mozart::SceneNodeOp::New());
        scene_node->op->get_scene()->scene_resource_id = scene_resource_id;
        update->nodes.insert(scene_node_id, std::move(scene_node));
        dim_node->child_node_ids.push_back(scene_node_id);
      }

      update->nodes.insert(dim_node_id, std::move(dim_node));
      container_node->child_node_ids.push_back(dim_node_id);
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

TileView::ViewData::ViewData(const std::string& url,
                             uint32_t key,
                             app::ApplicationControllerPtr controller)
    : url(url), key(key), controller(std::move(controller)) {}

TileView::ViewData::~ViewData() {}

}  // namespace examples
