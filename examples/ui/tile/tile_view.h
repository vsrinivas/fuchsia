// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_TILE_TILE_VIEW_H_
#define GARNET_EXAMPLES_UI_TILE_TILE_VIEW_H_

#include <map>
#include <memory>

#include "garnet/examples/ui/tile/tile_params.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/service_provider_bridge.h"
#include "lib/ui/presentation/fidl/presenter.fidl.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"

namespace examples {

class TileView : public mozart::BaseView,
                 public mozart::Presenter {
 public:
  TileView(mozart::ViewManagerPtr view_manager,
           f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
           app::ApplicationContext* application_context,
           const TileParams& tile_params);

  ~TileView() override;

 private:
  struct ViewData {
    explicit ViewData(const std::string& url,
                      uint32_t key,
                      app::ApplicationControllerPtr controller,
                      scenic_lib::Session* session);
    ~ViewData();

    const std::string url;
    const uint32_t key;
    app::ApplicationControllerPtr controller;
    scenic_lib::EntityNode host_node;

    mozart::ViewPropertiesPtr view_properties;
    mozart::ViewInfoPtr view_info;
  };

  // |BaseView|:
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info) override;
  void OnChildUnavailable(uint32_t child_key) override;
  void OnSceneInvalidated(ui::PresentationInfoPtr presentation_info) override;

  // |Presenter|:
  void Present(
      f1dl::InterfaceHandle<mozart::ViewOwner> view_owner,
      f1dl::InterfaceRequest<mozart::Presentation> presentation) override;

  // Set up environment with a |Presenter| service.
  // We launch apps with this environment.
  void CreateNestedEnvironment();

  // Launches initial list of views, passed as command line parameters.
  void ConnectViews();

  void AddChildView(f1dl::InterfaceHandle<mozart::ViewOwner> view_owner,
                    const std::string& url,
                    app::ApplicationControllerPtr);
  void RemoveChildView(uint32_t child_key);

  // Nested environment within which the apps started by TileView will run.
  app::ApplicationEnvironmentPtr env_;
  app::ApplicationEnvironmentControllerPtr env_controller_;
  app::ServiceProviderBridge service_provider_bridge_;
  app::ApplicationLauncherPtr env_launcher_;

  // Context inherited when TileView is launched.
  app::ApplicationContext* application_context_;

  // Parsed command-line parameters for this program.
  TileParams params_;

  // The container for all views.
  scenic_lib::EntityNode container_node_;

  // The key we will assigned to the next child view which is added.
  uint32_t next_child_view_key_ = 1u;

  // Map from keys to |ViewData|
  std::map<uint32_t, std::unique_ptr<ViewData>> views_;

  f1dl::BindingSet<mozart::Presenter> presenter_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TileView);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_UI_TILE_TILE_VIEW_H_
