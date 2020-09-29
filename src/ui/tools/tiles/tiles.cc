// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/tiles/tiles.h"

#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <cmath>

constexpr float kTileElevation = 5.f;

namespace tiles {

Tiles::Tiles(scenic::ViewContext view_context, std::vector<std::string> urls, int border)
    : scenic::BaseView(std::move(view_context), "Tiles"),
      launcher_(component_context()->svc()->Connect<fuchsia::sys::Launcher>()),
      background_node_(session()),
      container_node_(session()),
      border_(border) {
  // Create a simple background scene.
  scenic::Material background_material(session());
  background_material.SetColor(0xFF, 0xE4, 0xE1, 0xFF);  // Misty Rose
  background_node_.SetMaterial(background_material);
  root_node().AddChild(background_node_);
  root_node().AddChild(container_node_);

  // Add initial tiles.
  for (const auto& url : urls) {
    AddTileFromURL(url, true, {}, {});
  }

  // Make ourselves available as a |fuchsia.developer.TilesController|.
  component_context()->outgoing()->AddPublicService(tiles_binding_.GetHandler(this));
}

void Tiles::AddTileFromURL(std::string url, bool allow_focus, fidl::VectorPtr<std::string> args,
                           AddTileFromURLCallback callback) {
  FX_VLOGS(2) << "AddTile " << url;
  fuchsia::sys::ComponentControllerPtr controller;
  fuchsia::sys::LaunchInfo launch_info;
  std::shared_ptr<sys::ServiceDirectory> services =
      sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launch_info.url = url;
  launch_info.arguments = std::move(args);

  launcher_->CreateComponent(std::move(launch_info), controller.NewRequest());

  // Create a View from the launched component.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto view_provider = services->Connect<fuchsia::ui::app::ViewProvider>();
  view_provider->CreateView(std::move(view_token.value), nullptr, nullptr);

  uint32_t child_key = ++next_child_view_key_;
  AddTile(child_key, std::move(view_holder_token), url, std::move(controller), allow_focus);

  if (callback)
    callback(child_key);
}

void Tiles::AddTileFromViewProvider(std::string url,
                                    fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> provider,
                                    AddTileFromViewProviderCallback callback) {
  FX_VLOGS(2) << "AddTile " << url;

  // Create a View from the ViewProvider.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto view_provider = provider.Bind();
  view_provider->CreateView(std::move(view_token.value), nullptr, nullptr);

  uint32_t child_key = ++next_child_view_key_;
  AddTile(child_key, std::move(view_holder_token), url, nullptr /* controller */,
          true /* allow_focus */);

  if (callback)
    callback(child_key);
}

void Tiles::RemoveTile(uint32_t child_key) {
  auto it = views_.find(child_key);
  if (it == views_.end())
    return;

  it->second->host_node.Detach();
  views_.erase(it);

  Layout();
  InvalidateScene();
}

void Tiles::ListTiles(ListTilesCallback callback) {
  std::vector<uint32_t> child_keys;
  std::vector<std::string> child_urls;
  std::vector<fuchsia::ui::gfx::vec3> child_sizes;
  std::vector<bool> focusabilities;

  for (const auto& it : views_) {
    child_keys.push_back(it.first);
    child_urls.push_back(it.second->url);
    child_sizes.push_back(it.second->view_properties.bounding_box.max -
                          it.second->view_properties.bounding_box.min);
    focusabilities.push_back(it.second->view_properties.focus_change);
  };
  callback(std::move(child_keys), std::move(child_urls), std::move(child_sizes),
           std::move(focusabilities));
}

void Tiles::Quit() { exit(0); }

void Tiles::OnScenicError(std::string error) { FX_LOGS(ERROR) << "Scenic Error " << error; }

void Tiles::OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties /*old_properties*/) {
  scenic::Rectangle background_shape(session(), logical_size().x, logical_size().y);
  background_node_.SetShape(background_shape);
  background_node_.SetTranslation(logical_size().x / 2.f, logical_size().y / 2.f, 0.f);

  Layout();
}

void Tiles::OnScenicEvent(fuchsia::ui::scenic::Event event) {
  switch (event.Which()) {
    case fuchsia::ui::scenic::Event::Tag::kGfx:
      switch (event.gfx().Which()) {
        case fuchsia::ui::gfx::Event::Tag::kViewDisconnected: {
          uint32_t view_holder_id = event.gfx().view_disconnected().view_holder_id;
          auto it = view_id_to_keys_.find(view_holder_id);
          FX_DCHECK(it != view_id_to_keys_.end());
          FX_LOGS(ERROR) << "Tiles::OnScenicEvent: View died unexpectedly, id=" << view_holder_id;

          RemoveTile(it->second);
          break;
        }
        default:
          break;
      }
      break;
    default:
      FX_DCHECK(false) << "Tiles::OnScenicEvent: Got an unhandled Scenic "
                          "event.";
      break;
  }
}

void Tiles::AddTile(uint32_t child_key, fuchsia::ui::views::ViewHolderToken view_holder_token,
                    const std::string& url, fuchsia::sys::ComponentControllerPtr controller,
                    bool allow_focus) {
  auto view_data = std::make_unique<ViewData>(
      url, allow_focus, std::move(controller), scenic::EntityNode(session()),
      scenic::ViewHolder(session(), std::move(view_holder_token), "Tiles Embedder"));

  container_node_.AddChild(view_data->host_node);
  view_id_to_keys_.emplace(view_data->host_view_holder.id(), child_key);
  views_.emplace(child_key, std::move(view_data));

  Layout();
  InvalidateScene();
}

void Tiles::Layout() {
  if (views_.empty() || !has_logical_size())
    return;

  int num_tiles = static_cast<int>(views_.size());
  int columns = static_cast<int>(std::ceil(std::sqrt(num_tiles)));
  int rows = (columns + num_tiles - 1) / columns;
  float tile_height = logical_size().y / static_cast<float>(rows);

  auto view_it = views_.begin();
  for (int r = 0; r < rows; ++r) {
    // Each row has a tile per column, except possibly the last one.
    int tiles_in_row = columns;
    if (r == rows - 1 && (num_tiles % columns) != 0)
      tiles_in_row = num_tiles % columns;

    float tile_width = logical_size().x / static_cast<float>(tiles_in_row);
    float inset = std::min({static_cast<float>(border_), tile_width / 3.f, tile_height / 3.f});

    for (int c = 0; c < tiles_in_row; ++c) {
      ViewData* tile = view_it->second.get();

      fuchsia::ui::gfx::ViewProperties view_properties = fuchsia::ui::gfx::ViewProperties{
          .bounding_box =
              fuchsia::ui::gfx::BoundingBox{
                  .min = fuchsia::ui::gfx::vec3{.x = 0.f, .y = 0.f, .z = -logical_size().z},
                  .max =
                      fuchsia::ui::gfx::vec3{
                          .x = tile_width - 2 * inset, .y = tile_height - 2 * inset, .z = 0.f},
              },
          .inset_from_min = fuchsia::ui::gfx::vec3{.x = 0.f, .y = 0.f, .z = 0.f},
          .inset_from_max = fuchsia::ui::gfx::vec3{.x = 0.f, .y = 0.f, .z = 0.f},
          // Preserve this across |Layout| calls.
          .focus_change = tile->view_properties.focus_change,
      };
      tile->host_node.SetTranslation(static_cast<float>(c) * tile_width + inset,
                                     static_cast<float>(r) * tile_height + inset, -kTileElevation);
      if (!fidl::Equals(tile->view_properties, view_properties)) {
        tile->host_view_holder.SetViewProperties(view_properties);
        tile->view_properties = std::move(view_properties);
      }
      ++view_it;
    }
  }
}

Tiles::ViewData::ViewData(const std::string& url, bool allow_focus,
                          fuchsia::sys::ComponentControllerPtr controller, scenic::EntityNode node,
                          scenic::ViewHolder view_holder)
    : url(url),
      controller(std::move(controller)),
      host_node(std::move(node)),
      host_view_holder(std::move(view_holder)) {
  view_properties.focus_change = allow_focus;
  host_node.Attach(host_view_holder);
}

}  // namespace tiles
