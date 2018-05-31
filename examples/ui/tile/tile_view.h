// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_TILE_TILE_VIEW_H_
#define GARNET_EXAMPLES_UI_TILE_TILE_VIEW_H_

#include <map>
#include <memory>

#include <component/cpp/fidl.h>
#include <presentation/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include "garnet/examples/ui/tile/tile_params.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/service_provider_bridge.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"

namespace examples {

class TileView : public mozart::BaseView, public presentation::Presenter {
 public:
  TileView(::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner> view_owner_request,
           component::ApplicationContext* application_context,
           const TileParams& tile_params);

  ~TileView() override;

 private:
  struct ViewData {
    explicit ViewData(const std::string& url, uint32_t key,
                      component::ComponentControllerPtr controller,
                      scenic_lib::Session* session);
    ~ViewData();

    const std::string url;
    const uint32_t key;
    component::ComponentControllerPtr controller;
    scenic_lib::EntityNode host_node;

    ::fuchsia::ui::views_v1::ViewProperties view_properties;
    ::fuchsia::ui::views_v1::ViewInfo view_info;
  };

  // |BaseView|:
  void OnChildAttached(uint32_t child_key,
                       ::fuchsia::ui::views_v1::ViewInfo child_view_info) override;
  void OnChildUnavailable(uint32_t child_key) override;
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  // |Presenter|:
  void Present(
      fidl::InterfaceHandle<::fuchsia::ui::views_v1_token::ViewOwner> view_owner,
      fidl::InterfaceRequest<presentation::Presentation> presentation) override;
  void HACK_SetRendererParams(
      bool enable_clipping,
      ::fidl::VectorPtr<fuchsia::ui::gfx::RendererParam> params) override{};

  // Set up environment with a |Presenter| service.
  // We launch apps with this environment.
  void CreateNestedEnvironment();

  // Launches initial list of views, passed as command line parameters.
  void ConnectViews();

  void AddChildView(fidl::InterfaceHandle<::fuchsia::ui::views_v1_token::ViewOwner> view_owner,
                    const std::string& url, component::ComponentControllerPtr);
  void RemoveChildView(uint32_t child_key);

  // Nested environment within which the apps started by TileView will run.
  component::EnvironmentPtr env_;
  component::EnvironmentControllerPtr env_controller_;
  component::ServiceProviderBridge service_provider_bridge_;
  component::ApplicationLauncherPtr env_launcher_;

  // Context inherited when TileView is launched.
  component::ApplicationContext* application_context_;

  // Parsed command-line parameters for this program.
  TileParams params_;

  // The container for all views.
  scenic_lib::EntityNode container_node_;

  // The key we will assigned to the next child view which is added.
  uint32_t next_child_view_key_ = 1u;

  // Map from keys to |ViewData|
  std::map<uint32_t, std::unique_ptr<ViewData>> views_;

  fidl::BindingSet<presentation::Presenter> presenter_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TileView);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_UI_TILE_TILE_VIEW_H_
