// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_TILE_TILE_VIEW_H_
#define GARNET_EXAMPLES_UI_TILE_TILE_VIEW_H_

#include <fs/pseudo-dir.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <zx/eventpair.h>
#include <map>
#include <memory>

#include "garnet/examples/ui/tile/tile_params.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/ui/base_view/cpp/base_view.h"
#include "lib/ui/base_view/cpp/v1_base_view.h"
#include "lib/ui/scenic/cpp/resources.h"

namespace examples {

class TileView : public scenic::V1BaseView,
                 public fuchsia::ui::policy::Presenter {
 public:
  TileView(scenic::ViewContext context, TileParams tile_params);

  ~TileView() override;

 private:
  struct ViewData {
    explicit ViewData(uint32_t key,
                      fuchsia::sys::ComponentControllerPtr controller,
                      scenic::Session* session);
    ~ViewData();

    const uint32_t key;
    fuchsia::sys::ComponentControllerPtr controller;
    scenic::EntityNode host_node;
    scenic::ShapeNode clip_shape_node;

    fuchsia::ui::viewsv1::ViewProperties view_properties;
    fuchsia::ui::viewsv1::ViewInfo view_info;
  };

  // |scenic::V1BaseView|
  void OnChildAttached(
      uint32_t child_key,
      ::fuchsia::ui::viewsv1::ViewInfo child_view_info) override;
  void OnChildUnavailable(uint32_t child_key) override;
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  // |fuchsia::ui::policy::Presenter|
  void Present2(zx::eventpair view_owner_token,
                fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                    presentation) final;
  void HACK_SetRendererParams(
      bool enable_clipping,
      std::vector<fuchsia::ui::gfx::RendererParam> params) override {}

  // Set up environment with a |Presenter| service.
  // We launch apps with this environment.
  void CreateNestedEnvironment();

  // Launches initial list of views, passed as command line parameters.
  void ConnectViews();

  zx::channel OpenAsDirectory();

  void AddChildView(zx::eventpair view_owner_token,
                    fuchsia::sys::ComponentControllerPtr);
  void RemoveChildView(uint32_t child_key);

  // Nested environment within which the apps started by TileView will run.
  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> services_dir_;
  fuchsia::sys::LauncherPtr env_launcher_;

  // Parsed command-line parameters for this program.
  TileParams params_;

  // The container for all views.
  scenic::EntityNode container_node_;

  // The key we will assigned to the next child view which is added.
  uint32_t next_child_view_key_ = 1u;

  // Map from keys to |ViewData|
  std::map<uint32_t, std::unique_ptr<ViewData>> views_;

  fidl::BindingSet<fuchsia::ui::policy::Presenter> presenter_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TileView);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_UI_TILE_TILE_VIEW_H_
