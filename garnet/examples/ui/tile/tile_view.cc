// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/tile/tile_view.h"

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/split_string.h>
#include <lib/svc/cpp/services.h>

namespace examples {

TileView::TileView(scenic::ViewContext context, TileParams params)
    : V1BaseView(std::move(context), "Tile"),
      vfs_(async_get_default_dispatcher()),
      services_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      params_(std::move(params)),
      container_node_(session()) {
  parent_node().AddChild(container_node_);

  CreateNestedEnvironment();
  ConnectViews();
}

void TileView::Present2(
    zx::eventpair view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation) {
  AddChildView(std::move(view_holder_token), nullptr);
}

void TileView::ConnectViews() {
  for (const auto& url : params_.view_urls) {
    component::Services services;
    fuchsia::sys::ComponentControllerPtr controller;

    fuchsia::sys::LaunchInfo launch_info;

    // Pass arguments to children, if there are any.
    std::vector<std::string> split_url = fxl::SplitStringCopy(
        url, " ", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
    FXL_CHECK(split_url.size() >= 1);
    launch_info.url = split_url[0];
    launch_info.directory_request = services.NewRequest();

    if (split_url.size() > 1) {
      launch_info.arguments = fidl::VectorPtr<std::string>::New(0);
      for (auto it = split_url.begin() + 1; it != split_url.end(); it++) {
        launch_info.arguments.push_back(*it);
      }
    }

    // |env_launcher_| launches the component with our nested environment.
    env_launcher_->CreateComponent(std::move(launch_info),
                                   controller.NewRequest());

    // Create a View from the launched component.
    zx::eventpair view_token, view_holder_token;
    if (zx::eventpair::create(0u, &view_token, &view_holder_token) != ZX_OK)
      FXL_NOTREACHED() << "Failed to create view tokens";
    auto view_provider =
        services.ConnectToService<fuchsia::ui::app::ViewProvider>();
    view_provider->CreateView(std::move(view_token), nullptr, nullptr);

    // Add the view, which increments child_key_.
    AddChildView(std::move(view_holder_token), std::move(controller));
  }
}

zx::channel TileView::OpenAsDirectory() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  if (vfs_.ServeDirectory(services_dir_, std::move(h1)) != ZX_OK)
    return zx::channel();
  return h2;
}

void TileView::CreateNestedEnvironment() {
  // Add a binding for the presenter service
  auto service = fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
    presenter_bindings_.AddBinding(
        this, fidl::InterfaceRequest<fuchsia::ui::policy::Presenter>(
                  std::move(channel)));
    return ZX_OK;
  }));
  services_dir_->AddEntry(fuchsia::ui::policy::Presenter::Name_, service);

  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  service_list->names.push_back(fuchsia::ui::policy::Presenter::Name_);
  service_list->host_directory = OpenAsDirectory();
  startup_context()->environment()->CreateNestedEnvironment(
      env_.NewRequest(), env_controller_.NewRequest(), "tile",
      std::move(service_list), {.inherit_parent_services = true});
  env_->GetLauncher(env_launcher_.NewRequest());
}

void TileView::OnChildAttached(
    uint32_t child_key, ::fuchsia::ui::viewsv1::ViewInfo child_view_info) {
  auto it = views_.find(child_key);
  FXL_DCHECK(it != views_.end());

  ViewData* view_data = it->second.get();
  view_data->view_info = std::move(child_view_info);
}

void TileView::OnChildUnavailable(uint32_t child_key) {
  FXL_LOG(ERROR) << "View died unexpectedly: child_key=" << child_key;
  RemoveChildView(child_key);
}

void TileView::AddChildView(zx::eventpair view_holder_token,
                            fuchsia::sys::ComponentControllerPtr controller) {
  const uint32_t view_key = next_child_view_key_++;

  auto view_data =
      std::make_unique<ViewData>(view_key, std::move(controller), session());

  zx::eventpair host_import_token;
  view_data->host_node.ExportAsRequest(&host_import_token);
  container_node_.AddChild(view_data->host_node);

  view_data->host_node.AddPart(view_data->clip_shape_node);
  view_data->host_node.SetClip(0, true);

  views_.emplace(view_key, std::move(view_data));

  GetViewContainer()->AddChild2(view_key, std::move(view_holder_token),
                                std::move(host_import_token));
  InvalidateScene();
}

void TileView::RemoveChildView(uint32_t child_key) {
  auto it = views_.find(child_key);
  FXL_DCHECK(it != views_.end());

  it->second->host_node.Detach();
  views_.erase(it);

  GetViewContainer()->RemoveChild2(child_key, zx::eventpair());
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

    ::fuchsia::ui::viewsv1::ViewProperties view_properties;
    view_properties.view_layout = ::fuchsia::ui::viewsv1::ViewLayout::New();
    view_properties.view_layout->size.width = layout_bounds.width;
    view_properties.view_layout->size.height = layout_bounds.height;

    if (view_data->view_properties != view_properties) {
      ::fuchsia::ui::viewsv1::ViewProperties view_properties_clone;
      view_properties.Clone(&view_properties_clone);
      view_data->view_properties = std::move(view_properties_clone);
      GetViewContainer()->SetChildProperties(
          it->first, fidl::MakeOptional(std::move(view_properties)));
    }

    view_data->host_node.SetTranslationRH(layout_bounds.x, layout_bounds.y, 0u);

    // Clip
    scenic::Rectangle shape(session(),            // session
                            layout_bounds.width,  // width
                            layout_bounds.height  // height
    );
    view_data->clip_shape_node.SetShape(shape);
    view_data->clip_shape_node.SetTranslationRH(
        layout_bounds.width * 0.5f, layout_bounds.height * 0.5f, 0.f);
    ;
  }
}

TileView::ViewData::ViewData(uint32_t key,
                             fuchsia::sys::ComponentControllerPtr controller,
                             scenic::Session* session)
    : key(key),
      controller(std::move(controller)),
      host_node(session),
      clip_shape_node(session) {}

}  // namespace examples
