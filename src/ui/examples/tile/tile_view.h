// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_TILE_TILE_VIEW_H_
#define SRC_UI_EXAMPLES_TILE_TILE_VIEW_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/zx/eventpair.h>

#include <map>
#include <memory>

#include <fs/pseudo_dir.h>
#include <fs/synchronous_vfs.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/ui/base_view/base_view.h"
#include "src/ui/examples/tile/tile_params.h"

namespace examples {

class TileView : public scenic::BaseView, public fuchsia::ui::policy::Presenter {
 public:
  TileView(scenic::ViewContext context, TileParams tile_params);

  ~TileView() override = default;

 private:
  struct ViewData {
    explicit ViewData(std::string label, fuchsia::ui::views::ViewHolderToken view_holder_token,
                      fuchsia::sys::ComponentControllerPtr controller, scenic::Session* session);
    ~ViewData() = default;

    fuchsia::sys::ComponentControllerPtr controller;
    scenic::EntityNode host_node;
    scenic::ShapeNode clip_shape_node;
    scenic::ViewHolder view_holder;

    float width;
    float height;
  };

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FXL_LOG(ERROR) << "Scenic Error " << error; }

  void OnChildAttached(uint32_t view_holder_id);

  void OnChildUnavailable(uint32_t view_holder_id);

  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  // |scenic::BaseView|
  void OnScenicEvent(fuchsia::ui::scenic::Event event) override;

  // |fuchsia::ui::policy::Presenter|
  void PresentView(fuchsia::ui::views::ViewHolderToken view_holder_token,
                   fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation) final;

  // |fuchsia::ui::policy::Presenter|
  void PresentOrReplaceView(
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) final;

  void HACK_SetRendererParams(bool enable_clipping,
                              std::vector<fuchsia::ui::gfx::RendererParam> params) override {}

  // Set up environment with a |Presenter| service.
  // We launch apps with this environment.
  void CreateNestedEnvironment();

  // Launches initial list of views, passed as command line parameters.
  void ConnectViews();

  zx::channel OpenAsDirectory();

  void AddChildView(std::string label, fuchsia::ui::views::ViewHolderToken view_holder_token,
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

  // Map from ViewHolderIds to |ViewData|
  std::map<uint32_t, std::unique_ptr<ViewData>> views_;

  fidl::BindingSet<fuchsia::ui::policy::Presenter> presenter_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TileView);
};

}  // namespace examples

#endif  // SRC_UI_EXAMPLES_TILE_TILE_VIEW_H_
