// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/tile/tile_view.h"

#include <lib/fdio/util.h>

#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"
#include "lib/ui/geometry/cpp/geometry_util.h"

namespace examples {

TileView::TileView(
    ::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
        view_owner_request,
    component::StartupContext* startup_context, const TileParams& params)
    : BaseView(std::move(view_manager), std::move(view_owner_request), "Tile"),
      startup_context_(startup_context),
      params_(params),
      container_node_(session()) {
  parent_node().AddChild(container_node_);

  CreateNestedEnvironment();
  ConnectViews();
}

TileView::~TileView() {}

void TileView::Present(
    fidl::InterfaceHandle<::fuchsia::ui::views_v1_token::ViewOwner>
        child_view_owner,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation) {
  const std::string empty_url;
  AddChildView(std::move(child_view_owner), empty_url, nullptr);
}

void TileView::ConnectViews() {
  for (const auto& url : params_.view_urls) {
    component::Services services;
    fuchsia::sys::ComponentControllerPtr controller;

    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    launch_info.directory_request = services.NewRequest();

    // |env_launcher_| launches the app with our nested environment.
    env_launcher_->CreateComponent(std::move(launch_info),
                                   controller.NewRequest());

    // Get the view provider back from the launched app.
    auto view_provider =
        services.ConnectToService<::fuchsia::ui::views_v1::ViewProvider>();

    fidl::InterfaceHandle<::fuchsia::ui::views_v1_token::ViewOwner>
        child_view_owner;
    view_provider->CreateView(child_view_owner.NewRequest(), nullptr);

    // Add the view, which increments child_key_.
    AddChildView(std::move(child_view_owner), url, std::move(controller));
  }
}

void TileView::CreateNestedEnvironment() {
  startup_context_->environment()->CreateNestedEnvironment(
      service_provider_bridge_.OpenAsDirectory(), env_.NewRequest(),
      env_controller_.NewRequest(), "tile");
  env_->GetLauncher(env_launcher_.NewRequest());

  // Add a binding for the presenter service
  service_provider_bridge_.AddService<fuchsia::ui::policy::Presenter>(
      [this](fidl::InterfaceRequest<fuchsia::ui::policy::Presenter> request) {
        presenter_bindings_.AddBinding(this, std::move(request));
      });

  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return;
  startup_context_->environment()->GetDirectory(std::move(h1));
  service_provider_bridge_.set_backing_dir(std::move(h2));
}

void TileView::OnChildAttached(
    uint32_t child_key, ::fuchsia::ui::views_v1::ViewInfo child_view_info) {
  auto it = views_.find(child_key);
  FXL_DCHECK(it != views_.end());

  ViewData* view_data = it->second.get();
  view_data->view_info = std::move(child_view_info);
}

void TileView::OnChildUnavailable(uint32_t child_key) {
  FXL_LOG(ERROR) << "View died unexpectedly: child_key=" << child_key;
  RemoveChildView(child_key);
}

void TileView::AddChildView(
    fidl::InterfaceHandle<::fuchsia::ui::views_v1_token::ViewOwner>
        child_view_owner,
    const std::string& url, fuchsia::sys::ComponentControllerPtr controller) {
  const uint32_t view_key = next_child_view_key_++;

  auto view_data = std::make_unique<ViewData>(url, view_key,
                                              std::move(controller), session());

  zx::eventpair host_import_token;
  view_data->host_node.ExportAsRequest(&host_import_token);
  container_node_.AddChild(view_data->host_node);
  views_.emplace(view_key, std::move(view_data));

  GetViewContainer()->AddChild(view_key, std::move(child_view_owner),
                               std::move(host_import_token));
  InvalidateScene();
}

void TileView::RemoveChildView(uint32_t child_key) {
  auto it = views_.find(child_key);
  FXL_DCHECK(it != views_.end());

  it->second->host_node.Detach();
  views_.erase(it);

  GetViewContainer()->RemoveChild(child_key, nullptr);
  InvalidateScene();
}

void TileView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size() || views_.empty())
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

    fuchsia::math::RectF layout_bounds;
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

    ::fuchsia::ui::views_v1::ViewProperties view_properties;
    view_properties.view_layout = ::fuchsia::ui::views_v1::ViewLayout::New();
    view_properties.view_layout->size.width = layout_bounds.width;
    view_properties.view_layout->size.height = layout_bounds.height;

    if (view_data->view_properties != view_properties) {
      ::fuchsia::ui::views_v1::ViewProperties view_properties_clone;
      view_properties.Clone(&view_properties_clone);
      view_data->view_properties = std::move(view_properties_clone);
      GetViewContainer()->SetChildProperties(
          it->first, fidl::MakeOptional(std::move(view_properties)));
    }

    view_data->host_node.SetTranslation(layout_bounds.x, layout_bounds.y, 0u);
  }
}

TileView::ViewData::ViewData(const std::string& url, uint32_t key,
                             fuchsia::sys::ComponentControllerPtr controller,
                             scenic::Session* session)
    : url(url),
      key(key),
      controller(std::move(controller)),
      host_node(session) {}

TileView::ViewData::~ViewData() {}

}  // namespace examples
