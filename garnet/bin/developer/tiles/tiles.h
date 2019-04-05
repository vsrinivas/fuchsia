// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEVELOPER_TILES_TILES_H_
#define GARNET_BIN_DEVELOPER_TILES_TILES_H_

#include <fuchsia/developer/tiles/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/scenic/cpp/id.h>
#include <lib/ui/scenic/cpp/resources.h>

namespace tiles {

class Tiles : public fuchsia::developer::tiles::Controller,
              public scenic::BaseView {
 public:
  Tiles(scenic::ViewContext view_context, std::vector<std::string> urls,
        int border = 0);
  ~Tiles() override = default;

 private:
  struct ViewData {
    explicit ViewData(const std::string& url, bool allow_focus,
                      fuchsia::sys::ComponentControllerPtr controller,
                      scenic::EntityNode node, scenic::ViewHolder view_holder);
    ~ViewData() = default;

    const std::string url;
    fuchsia::sys::ComponentControllerPtr controller;
    fuchsia::ui::gfx::ViewProperties view_properties;
    scenic::EntityNode host_node;
    scenic::ViewHolder host_view_holder;
  };

  // |fuchsia::developer::tiles::Controller|
  void AddTileFromURL(std::string url, bool allow_focus,
                      fidl::VectorPtr<std::string> args,
                      AddTileFromURLCallback callback) final;
  void AddTileFromViewProvider(
      std::string url,
      fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> provider,
      AddTileFromViewProviderCallback callback) final;
  void RemoveTile(uint32_t key) final;
  void ListTiles(ListTilesCallback callback) final;
  void Quit() final;

  // |scenic::SessionListener|
  void OnScenicError(std::string error) final;

  // |scenic::BaseView|
  void OnPropertiesChanged(
      fuchsia::ui::gfx::ViewProperties old_properties) final;
  void OnScenicEvent(fuchsia::ui::scenic::Event) final;

  void AddTile(uint32_t child_key,
               fuchsia::ui::views::ViewHolderToken view_holder_token,
               const std::string& url, fuchsia::sys::ComponentControllerPtr,
               bool allow_focus);
  void Layout();

  fidl::BindingSet<fuchsia::developer::tiles::Controller> tiles_binding_;
  fuchsia::sys::LauncherPtr launcher_;

  std::unordered_map<uint32_t, std::unique_ptr<ViewData>> views_;
  std::unordered_map<scenic::ResourceId, uint32_t> view_id_to_keys_;
  scenic::ShapeNode background_node_;
  scenic::EntityNode container_node_;

  uint32_t next_child_view_key_ = 0;
  int border_ = 0;  // Border in logical pixels for each tile.
};

}  // namespace tiles

#endif  // GARNET_BIN_DEVELOPER_TILES_TILES_H_
