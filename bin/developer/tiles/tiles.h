// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEVELOPER_TILES_TILES_H_
#define GARNET_BIN_DEVELOPER_TILES_TILES_H_

#include <fuchsia/developer/tiles/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/svc/cpp/service_provider_bridge.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace tiles {

class Tiles : public fuchsia::ui::views_v1::ViewListener,
              public fuchsia::ui::views_v1::ViewContainerListener,
              public fuchsia::developer::tiles::Tiles {
 public:
  Tiles(::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
        fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
            view_owner_request,
        fuchsia::sys::StartupContext* startup_context, int border);

  ~Tiles() final;

 private:
  struct ViewData {
    explicit ViewData(const std::string& url, uint32_t key,
                      fuchsia::sys::ComponentControllerPtr controller,
                      scenic::Session* session);
    ~ViewData();

    const std::string url;
    const uint32_t key;
    fuchsia::sys::ComponentControllerPtr controller;
    scenic::EntityNode host_node;

    ::fuchsia::ui::views_v1::ViewProperties view_properties;
    ::fuchsia::ui::views_v1::ViewInfo view_info;
  };

  // |fuchsia::ui::views_v1::ViewListener|:
  void OnPropertiesChanged(fuchsia::ui::views_v1::ViewProperties properties,
                           OnPropertiesChangedCallback callback) final;

  // |fuchsia::ui::views_v1::ViewContainerListener|:
  void OnChildAttached(uint32_t child_key,
                       fuchsia::ui::views_v1::ViewInfo child_view_info,
                       OnChildAttachedCallback) final;
  void OnChildUnavailable(uint32_t child_key,
                          OnChildUnavailableCallback callback) final;

  // |fuchsia::developer::tiles::Tiles|:
  void AddTileFromURL(fidl::StringPtr url,
                      fidl::VectorPtr<fidl::StringPtr> args,
                      AddTileFromURLCallback callback) final;
  void AddTileFromViewProvider(
      fidl::StringPtr url,
      fidl::InterfaceHandle<::fuchsia::ui::views_v1::ViewProvider> provider,
      AddTileFromViewProviderCallback callback) final;
  void RemoveTile(uint32_t key) final;
  void ListTiles(ListTilesCallback callback) final;

  // Set up environment for launched apps
  void CreateNestedEnvironment();

  // Launches initial list of views, passed as command line parameters.

  void AddChildView(
      uint32_t child_key,
      fidl::InterfaceHandle<::fuchsia::ui::views_v1_token::ViewOwner>
          view_owner,
      const std::string& url, fuchsia::sys::ComponentControllerPtr);

  void InvalidateScene();
  void Layout();
  void PresentScene();

  // Context inherited when TileView is launched.
  fuchsia::sys::StartupContext* startup_context_;

  // Connection to the view manager and root view.
  fuchsia::ui::views_v1::ViewManagerPtr view_manager_;
  fidl::Binding<fuchsia::ui::views_v1::ViewListener> view_listener_binding_;
  fidl::Binding<fuchsia::ui::views_v1::ViewContainerListener>
      view_container_listener_binding_;
  fuchsia::ui::views_v1::ViewPtr view_;
  scenic::Session session_;

  scenic::ImportNode root_node_;
  scenic::ShapeNode background_node_;
  scenic::EntityNode container_node_;

  fuchsia::ui::views_v1::ViewContainerPtr view_container_;

  fidl::BindingSet<fuchsia::developer::tiles::Tiles> tiles_binding_;

  // Nested environment within which the apps started by Tiles will run.
  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fuchsia::sys::ServiceProviderBridge service_provider_bridge_;
  fuchsia::sys::LauncherPtr env_launcher_;

  fuchsia::math::SizeF size_;

  // The key we will assigned to the next child view which is added.
  uint32_t next_child_view_key_ = 1u;

  async::TaskClosure present_scene_task_;

  // Map from keys to |ViewData|
  std::map<uint32_t, std::unique_ptr<ViewData>> views_;

  // Border in logical pixels for each tile.
  int border_;
};

}  // namespace tiles

#endif  // GARNET_BIN_DEVELOPER_TILES_TILES_H_
