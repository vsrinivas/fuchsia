// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/developer/tiles/tiles.h"

#include <lib/async/default.h>
#include <lib/fidl/cpp/optional.h>
#include <src/lib/fxl/logging.h>
#include <lib/svc/cpp/services.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>
#include <src/lib/pkg_url/fuchsia_pkg_url.h>
#include <cmath>

constexpr float kTileElevation = 5.f;

namespace tiles {

Tiles::Tiles(sys::ComponentContext* startup_context,
             fuchsia::ui::views::ViewToken view_token,
             std::vector<std::string> urls, int border)
    : startup_context_(startup_context),
      root_view_listener_binding_(this),
      root_view_container_listener_binding_(this),
      session_(scenic::CreateScenicSessionPtrAndListenerRequest(
          startup_context_->svc()
              ->Connect<fuchsia::ui::scenic::Scenic>()
              .get())),
      root_node_(&session_),
      background_node_(&session_),
      container_node_(&session_),
      launcher_(startup_context_->svc()->Connect<fuchsia::sys::Launcher>()),
      present_scene_task_([this]() { PresentScene(); }),
      border_(border) {
  // Create a simple background scene.
  zx::eventpair root_export_token;
  scenic::Material background_material(&session_);
  background_material.SetColor(0xFF, 0xE4, 0xE1, 0xFF);  // Misty Rose
  background_node_.SetMaterial(background_material);
  root_node_.BindAsRequest(&root_export_token);
  root_node_.AddChild(background_node_);
  root_node_.AddChild(container_node_);

  // Create a View and export our scene from it.
  auto view_manager =
      startup_context_->svc()->Connect<fuchsia::ui::viewsv1::ViewManager>();
  view_manager->CreateView2(root_view_.NewRequest(),
                            std::move(view_token.value),
                            root_view_listener_binding_.NewBinding(),
                            std::move(root_export_token), "Tiles Root");

  // Listen for events from the View.
  root_view_->GetContainer(root_view_container_.NewRequest());
  root_view_container_->SetListener(
      root_view_container_listener_binding_.NewBinding());

  // Add initial tiles.
  for (const auto& url : urls) {
    AddTileFromURL(fidl::StringPtr(url), true, {}, {});
  }

  // Make ourselves available as a |fuchsia.developer.TilesController|.
  startup_context_->outgoing()->AddPublicService(
      tiles_binding_.GetHandler(this));
}

void Tiles::AddTileFromURL(std::string url, bool allow_focus,
                           fidl::VectorPtr<std::string> args,
                           AddTileFromURLCallback callback) {
  FXL_VLOG(2) << "AddTile " << url;
  component::Services services;
  fuchsia::sys::ComponentControllerPtr controller;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = url;
  launch_info.arguments = std::move(args);
  launch_info.directory_request = services.NewRequest();

  launcher_->CreateComponent(std::move(launch_info), controller.NewRequest());

  auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

  // Create a View from the launched component.
  auto view_provider =
      services.ConnectToService<fuchsia::ui::app::ViewProvider>();
  view_provider->CreateView(std::move(view_token.value), nullptr, nullptr);

  uint32_t child_key = next_child_view_key_++;
  AddChildView(child_key, std::move(view_holder_token), url,
               std::move(controller), allow_focus);

  if (callback)
    callback(child_key);
}

void Tiles::AddTileFromViewProvider(
    std::string url,
    fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> provider,
    AddTileFromViewProviderCallback callback) {
  FXL_VLOG(2) << "AddTile " << url;

  auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

  // Create a View from the ViewProvider.
  auto view_provider = provider.Bind();
  view_provider->CreateView(std::move(view_token.value), nullptr, nullptr);

  uint32_t child_key = next_child_view_key_++;
  AddChildView(child_key, std::move(view_holder_token), url,
               nullptr /* controller */, true /* allow_focus */);

  if (callback)
    callback(child_key);
}

void Tiles::RemoveTile(uint32_t child_key) {
  auto it = views_.find(child_key);
  if (it == views_.end())
    return;

  it->second->host_node.Detach();
  views_.erase(it);

  root_view_container_->RemoveChild2(child_key, zx::eventpair());
  InvalidateScene();
}

void Tiles::ListTiles(ListTilesCallback callback) {
  std::vector<uint32_t> child_keys;
  std::vector<std::string> child_urls;
  std::vector<fuchsia::math::SizeF> child_sizes;
  std::vector<bool> focusabilities;

  for (const auto& it : views_) {
    child_keys.push_back(it.first);
    child_urls.push_back(it.second->url);
    focusabilities.push_back(it.second->allow_focus);
    // We may not know every tile's size if we haven't laid everything out yet.
    if (it.second->view_properties.view_layout != nullptr) {
      child_sizes.push_back(it.second->view_properties.view_layout->size);
    } else {
      child_sizes.push_back(fuchsia::math::SizeF{});
    }
  }
  callback(fidl::VectorPtr<uint32_t>(std::move(child_keys)),
           fidl::VectorPtr<std::string>(std::move(child_urls)),
           fidl::VectorPtr<fuchsia::math::SizeF>(std::move(child_sizes)),
           fidl::VectorPtr<bool>(std::move(focusabilities)));
}

void Tiles::Quit() { exit(0); }

void Tiles::OnPropertiesChanged(fuchsia::ui::viewsv1::ViewProperties properties,
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
                            fuchsia::ui::viewsv1::ViewInfo child_view_info,
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
                         fuchsia::ui::views::ViewHolderToken view_holder_token,
                         const std::string& url,
                         fuchsia::sys::ComponentControllerPtr controller,
                         bool allow_focus) {
  auto view_data = std::make_unique<ViewData>(
      url, child_key, std::move(controller), &session_, allow_focus);

  zx::eventpair host_import_token;
  view_data->host_node.ExportAsRequest(&host_import_token);
  container_node_.AddChild(view_data->host_node);
  views_.emplace(child_key, std::move(view_data));

  root_view_container_->AddChild2(child_key, std::move(view_holder_token.value),
                                  std::move(host_import_token));
  InvalidateScene();
}

void Tiles::InvalidateScene() {
  if (present_scene_task_.is_pending())
    return;
  present_scene_task_.Post(async_get_default_dispatcher());
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

      fuchsia::ui::viewsv1::ViewProperties view_properties;
      view_properties.view_layout = ::fuchsia::ui::viewsv1::ViewLayout::New();
      view_properties.view_layout->size.width = tile_bounds.width;
      view_properties.view_layout->size.height = tile_bounds.height;
      view_properties.custom_focus_behavior =
          ::fuchsia::ui::viewsv1::CustomFocusBehavior::New();
      view_properties.custom_focus_behavior->allow_focus = tile->allow_focus;

      if (tile->view_properties != view_properties) {
        fuchsia::ui::viewsv1::ViewProperties view_properties_clone;
        view_properties.Clone(&view_properties_clone);
        tile->view_properties = std::move(view_properties_clone);
        root_view_container_->SetChildProperties(
            view_it->first, fidl::MakeOptional(std::move(view_properties)));
      }

      tile->host_node.SetTranslation(tile_bounds.x, tile_bounds.y,
                                     -kTileElevation);
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
                          scenic::Session* session, bool allow_focus)
    : url(url),
      key(key),
      allow_focus(allow_focus),
      controller(std::move(controller)),
      host_node(session) {}

Tiles::ViewData::~ViewData() {}

}  // namespace tiles
