// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/tile/tile_view.h"

#include <mxio/util.h>

#include "application/lib/app/connect.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/ftl/logging.h"

namespace examples {

TileView::TileView(mozart::ViewManagerPtr view_manager,
                   fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                   app::ApplicationContext* application_context,
                   const TileParams& params)
    : BaseView(std::move(view_manager), std::move(view_owner_request), "Tile"),
      env_host_binding_(this),
      application_context_(application_context),
      params_(params),
      container_node_(session()) {
  parent_node().AddChild(container_node_);

  CreateNestedEnvironment();
  ConnectViews();
}

TileView::~TileView() {}

void TileView::Present(
    fidl::InterfaceHandle<mozart::ViewOwner> child_view_owner) {
  const std::string empty_url;
  AddChildView(std::move(child_view_owner), empty_url, nullptr);
}

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
    AddChildView(std::move(child_view_owner), url, std::move(controller));
  }
}

void TileView::GetApplicationEnvironmentServices(
    fidl::InterfaceRequest<app::ServiceProvider> environment_services) {
  env_services_.AddBinding(std::move(environment_services));
}

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
        application_context_->ConnectToEnvironmentService(service_name,
                                                          std::move(channel));
      });
}

void TileView::OnPropertiesChanged(mozart::ViewPropertiesPtr old_properties) {
  UpdateScene();
}

void TileView::OnChildAttached(uint32_t child_key,
                               mozart::ViewInfoPtr child_view_info) {
  auto it = views_.find(child_key);
  FTL_DCHECK(it != views_.end());

  ViewData* view_data = it->second.get();
  view_data->view_info = std::move(child_view_info);
}

void TileView::OnChildUnavailable(uint32_t child_key) {
  FTL_LOG(ERROR) << "View died unexpectedly: child_key=" << child_key;
  RemoveChildView(child_key);
}

void TileView::AddChildView(
    fidl::InterfaceHandle<mozart::ViewOwner> child_view_owner,
    const std::string& url,
    app::ApplicationControllerPtr app_controller) {
  const uint32_t view_key = next_child_view_key_++;

  auto view_data = std::make_unique<ViewData>(
      url, view_key, std::move(app_controller), session());

  mx::eventpair host_import_token;
  view_data->host_node.ExportAsRequest(&host_import_token);
  container_node_.AddChild(view_data->host_node);
  views_.emplace(view_key, std::move(view_data));

  GetViewContainer()->AddChild(view_key, std::move(child_view_owner),
                               std::move(host_import_token));
  UpdateScene();
}

void TileView::RemoveChildView(uint32_t child_key) {
  auto it = views_.find(child_key);
  FTL_DCHECK(it != views_.end());

  it->second->host_node.Detach();
  views_.erase(it);

  GetViewContainer()->RemoveChild(child_key, nullptr);
  UpdateScene();
}

void TileView::UpdateScene() {
  if (!properties() || views_.empty())
    return;

  // Layout all children in a row.
  const bool vertical =
      (params_.orientation_mode == TileParams::OrientationMode::kVertical);

  uint32_t index = 0;
  uint32_t space = vertical ? logical_size().height : logical_size().width;
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

    mozart::RectF layout_bounds;
    if (vertical) {
      layout_bounds.x = 0;
      layout_bounds.y = offset;
      layout_bounds.width = logical_size().width;
      layout_bounds.height = extent;
    } else {
      layout_bounds.x = offset;
      layout_bounds.y = 0;
      layout_bounds.width = extent;
      layout_bounds.height = logical_size().height;
    }
    offset += extent;

    auto view_properties = mozart::ViewProperties::New();
    view_properties->view_layout = mozart::ViewLayout::New();
    view_properties->view_layout->size = mozart::SizeF::New();
    view_properties->view_layout->size->width = layout_bounds.width;
    view_properties->view_layout->size->height = layout_bounds.height;
    view_properties->view_layout->inset = mozart::InsetF::New();

    if (!view_data->view_properties.Equals(view_properties)) {
      view_data->view_properties = view_properties.Clone();
      GetViewContainer()->SetChildProperties(it->first,
                                             std::move(view_properties));
    }

    view_data->host_node.SetTranslation(layout_bounds.x, layout_bounds.y, 0u);
  }

  session()->Present(0, [](scenic::PresentationInfoPtr info) {});
}

TileView::ViewData::ViewData(const std::string& url,
                             uint32_t key,
                             app::ApplicationControllerPtr controller,
                             scenic_lib::Session* session)
    : url(url),
      key(key),
      controller(std::move(controller)),
      host_node(session) {}

TileView::ViewData::~ViewData() {}

}  // namespace examples
