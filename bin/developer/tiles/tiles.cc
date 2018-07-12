// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/developer/tiles/tiles.h"

#include <lib/async/default.h>

#include "lib/fidl/cpp/optional.h"

using ::fuchsia::ui::views_v1::ViewProperties;
using ::fuchsia::ui::views_v1::ViewProvider;
using ::fuchsia::ui::views_v1_token::ViewOwner;

constexpr float kTileElevation = 5.f;

namespace tiles {

namespace {

fuchsia::ui::scenic::ScenicPtr GetScenic(
    ::fuchsia::ui::views_v1::ViewManager* view_manager) {
  fuchsia::ui::scenic::ScenicPtr scenic;
  view_manager->GetScenic(scenic.NewRequest());
  return scenic;
}

fuchsia::ui::scenic::SessionPtr GetSession(
    fuchsia::ui::scenic::Scenic* scenic) {
  fuchsia::ui::scenic::SessionPtr session;
  scenic->CreateSession(session.NewRequest(), nullptr);
  return session;
}

}  // anonymous namespace

Tiles::Tiles(::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
             fidl::InterfaceRequest<ViewOwner> view_owner_request,
             fuchsia::sys::StartupContext* startup_context, int border)
    : startup_context_(startup_context),
      view_manager_(std::move(view_manager)),
      view_listener_binding_(this),
      view_container_listener_binding_(this),
      session_(GetSession(GetScenic(view_manager_.get()).get())),
      root_node_(&session_),
      background_node_(&session_),
      container_node_(&session_),
      present_scene_task_([this]() { PresentScene(); }),
      border_(border) {
  zx::eventpair root_export_token;
  root_node_.BindAsRequest(&root_export_token);

  scenic::Material background_material(&session_);
  background_material.SetColor(0xFF, 0xE4, 0xE1, 0xFF);  // Misty Rose
  background_node_.SetMaterial(background_material);
  root_node_.AddChild(background_node_);
  root_node_.AddChild(container_node_);

  view_manager_->CreateView(view_.NewRequest(), std::move(view_owner_request),
                            view_listener_binding_.NewBinding(),
                            std::move(root_export_token), "Tile");

  view_->GetContainer(view_container_.NewRequest());
  view_container_->SetListener(view_container_listener_binding_.NewBinding());

  startup_context_->outgoing().AddPublicService(
      tiles_binding_.GetHandler(this));
  CreateNestedEnvironment();
}

Tiles::~Tiles() = default;

void Tiles::CreateNestedEnvironment() {
  startup_context_->environment()->CreateNestedEnvironment(
      service_provider_bridge_.OpenAsDirectory(), env_.NewRequest(),
      env_controller_.NewRequest(), "tile");
  env_->GetLauncher(env_launcher_.NewRequest());

  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return;
  startup_context_->environment()->GetDirectory(std::move(h1));
  service_provider_bridge_.set_backing_dir(std::move(h2));
}

// |Tiles|:
void Tiles::AddTileFromURL(fidl::StringPtr url,
                           fidl::VectorPtr<fidl::StringPtr> args,
                           AddTileFromURLCallback callback) {
  FXL_VLOG(2) << "AddTile " << url;
  fuchsia::sys::Services services;
  fuchsia::sys::ComponentControllerPtr controller;

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = url;
  launch_info.arguments = std::move(args);
  launch_info.directory_request = services.NewRequest();

  // |env_launcher_| launches the app with our nested environment.
  env_launcher_->CreateComponent(std::move(launch_info),
                                 controller.NewRequest());

  // Get the view provider back from the launched app.
  auto view_provider = services.ConnectToService<ViewProvider>();

  fidl::InterfaceHandle<ViewOwner> child_view_owner;
  view_provider->CreateView(child_view_owner.NewRequest(), nullptr);

  uint32_t child_key = next_child_view_key_++;

  AddChildView(child_key, std::move(child_view_owner), url,
               std::move(controller));

  callback(child_key);
}

void Tiles::AddTileFromViewProvider(
    fidl::StringPtr url, fidl::InterfaceHandle<ViewProvider> provider,
    AddTileFromViewProviderCallback callback) {
  FXL_LOG(ERROR) << "Not implemented";
  callback(0);
}

void Tiles::RemoveTile(uint32_t child_key) {
  auto it = views_.find(child_key);
  if (it == views_.end())
    return;

  it->second->host_node.Detach();
  views_.erase(it);

  view_container_->RemoveChild(child_key, nullptr);
  InvalidateScene();
}

void Tiles::ListTiles(ListTilesCallback callback) {
  std::vector<uint32_t> child_keys;
  std::vector<fidl::StringPtr> child_urls;
  std::vector<fuchsia::math::SizeF> child_sizes;

  for (const auto& it : views_) {
    child_keys.push_back(it.first);
    child_urls.push_back(it.second->url);
    // We may not know every tile's size if we haven't laid everything out yet.
    if (it.second->view_properties.view_layout != nullptr) {
      child_sizes.push_back(it.second->view_properties.view_layout->size);
    } else {
      child_sizes.push_back(fuchsia::math::SizeF{});
    }
  }
  callback(fidl::VectorPtr<uint32_t>(std::move(child_keys)),
           fidl::VectorPtr<fidl::StringPtr>(std::move(child_urls)),
           fidl::VectorPtr<fuchsia::math::SizeF>(std::move(child_sizes)));
}

void Tiles::OnPropertiesChanged(ViewProperties properties,
                                OnPropertiesChangedCallback callback) {
  size_ = properties.view_layout->size;
  scenic::Rectangle background_shape(&session_, size_.width, size_.height);
  background_node_.SetShape(background_shape);
  float center_x = size_.width / 2.f;
  float center_y = size_.height / 2.f;
  background_node_.SetTranslation(center_x, center_y, 0.f);
  callback();
  InvalidateScene();
}

void Tiles::OnChildAttached(uint32_t child_key,
                            ::fuchsia::ui::views_v1::ViewInfo child_view_info,
                            OnChildAttachedCallback callback) {
  auto it = views_.find(child_key);
  FXL_DCHECK(it != views_.end());

  ViewData* view_data = it->second.get();
  view_data->view_info = std::move(child_view_info);
  callback();
  InvalidateScene();
}

void Tiles::OnChildUnavailable(uint32_t child_key,
                               OnChildUnavailableCallback callback) {
  FXL_LOG(ERROR) << "View died unexpectedly: " << child_key;

  RemoveTile(child_key);
  callback();
  InvalidateScene();
}

void Tiles::AddChildView(uint32_t child_key,
                         fidl::InterfaceHandle<ViewOwner> child_view_owner,
                         const std::string& url,
                         fuchsia::sys::ComponentControllerPtr controller) {
  auto view_data = std::make_unique<ViewData>(url, child_key,
                                              std::move(controller), &session_);

  zx::eventpair host_import_token;
  view_data->host_node.ExportAsRequest(&host_import_token);
  container_node_.AddChild(view_data->host_node);
  views_.emplace(child_key, std::move(view_data));

  view_container_->AddChild(child_key, std::move(child_view_owner),
                            std::move(host_import_token));
  InvalidateScene();
}

void Tiles::InvalidateScene() {
  if (present_scene_task_.is_pending())
    return;
  present_scene_task_.Post(async_get_default());
}

static void Inset(fuchsia::math::RectF* rect, int border) {
  float inset = std::min(
      {static_cast<float>(border), rect->width / 3.f, rect->height / 3.f});
  rect->x += inset;
  rect->y += inset;
  rect->width -= 2 * inset;
  rect->height -= 2 * inset;
}

void Tiles::Layout() {
  if (views_.empty())
    return;

  int num_tiles = views_.size();

  int columns = std::ceil(std::sqrt(num_tiles));
  int rows = (columns + num_tiles - 1) / columns;
  float tile_height = size_.height / rows;

  auto view_it = views_.begin();

  for (int r = 0; r < rows; ++r) {
    // Each row has a tile per column, except possibly the last one.
    int tiles_in_row = columns;
    if (r == rows - 1 && (num_tiles % columns) != 0)
      tiles_in_row = num_tiles % columns;

    float tile_width = size_.width / tiles_in_row;

    for (int c = 0; c < tiles_in_row; ++c) {
      fuchsia::math::RectF tile_bounds;
      tile_bounds.x = c * tile_width;
      tile_bounds.y = r * tile_height;
      tile_bounds.width = tile_width;
      tile_bounds.height = tile_height;

      Inset(&tile_bounds, border_);

      ViewData* tile = view_it->second.get();

      ViewProperties view_properties;
      view_properties.view_layout = ::fuchsia::ui::views_v1::ViewLayout::New();
      view_properties.view_layout->size.width = tile_bounds.width;
      view_properties.view_layout->size.height = tile_bounds.height;

      if (tile->view_properties != view_properties) {
        ViewProperties view_properties_clone;
        view_properties.Clone(&view_properties_clone);
        tile->view_properties = std::move(view_properties_clone);
        view_container_->SetChildProperties(
            view_it->first, fidl::MakeOptional(std::move(view_properties)));
      }

      tile->host_node.SetTranslation(tile_bounds.x, tile_bounds.y,
                                     kTileElevation);
      ++view_it;
    }
  }
}

void Tiles::PresentScene() {
  if (!size_.width || !size_.height)
    return;

  Layout();

  zx_time_t presentation_time = 0;
  session_.Present(presentation_time,
                   [](fuchsia::images::PresentationInfo info) {});
}

Tiles::ViewData::ViewData(const std::string& url, uint32_t key,
                          fuchsia::sys::ComponentControllerPtr controller,
                          scenic::Session* session)
    : url(url),
      key(key),
      controller(std::move(controller)),
      host_node(session) {}

Tiles::ViewData::~ViewData() {}

}  // namespace tiles
