// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_PRESENTATION_H_
#define SRC_UI_BIN_ROOT_PRESENTER_PRESENTATION_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/ui/scenic/cpp/id.h>
#include <lib/ui/scenic/cpp/resources.h>

#include <map>

#include "src/lib/ui/input/device_state.h"
#include "src/lib/ui/input/input_device_impl.h"
#include "src/ui/bin/root_presenter/constants.h"
#include "src/ui/bin/root_presenter/displays/display_metrics.h"
#include "src/ui/bin/root_presenter/displays/display_model.h"
#include "src/ui/bin/root_presenter/injector.h"
#include "src/ui/bin/root_presenter/injector_config_setup.h"
#include "src/ui/bin/root_presenter/inspect.h"
#include "src/ui/bin/root_presenter/safe_presenter.h"

namespace root_presenter {

// This class creates a root ViewHolder and sets up rendering of a new scene to
// display the graphical content of the view passed to |Present()|.  It also wires up input
// dispatch.
//
// The root ViewHolder has the presented (content) view as its child.
//
// The scene's node tree has the following structure:
// + Scene
//   + RootNode
//     + ViewHolder
//       + link: Content view's actual content
//
class Presentation : fuchsia::ui::policy::Presentation,
                     fuchsia::accessibility::MagnificationHandler,
                     fuchsia::ui::accessibility::view::Registry {
 public:
  Presentation(inspect::Node inspect_node, sys::ComponentContext* component_context,
               fuchsia::ui::scenic::Scenic* scenic, scenic::Session* session,
               scenic::ResourceId compositor_id,
               fuchsia::ui::views::ViewHolderToken view_holder_token,
               fuchsia::ui::views::ViewRef client_view_ref,
               fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request,
               SafePresenter* safe_presenter, int32_t display_startup_rotation_adjustment,
               fit::function<void()> on_client_death,
               fit::function<void(fuchsia::ui::views::ViewRef)> request_focus);
  ~Presentation() override;

  void RegisterWithMagnifier(fuchsia::accessibility::Magnifier* magnifier);

  void OnReport(uint32_t device_id, fuchsia::ui::input::InputReport report);
  void OnDeviceAdded(ui_input::InputDeviceImpl* input_device);
  void OnDeviceRemoved(uint32_t device_id);

  const scenic::Layer& layer() const { return layer_; }

  bool is_initialized() const { return display_model_initialized_ && scene_initialized_; }

 private:
  // |fuchsia::ui::policy::Presentation|
  void CapturePointerEventsHACK(
      fidl::InterfaceHandle<fuchsia::ui::policy::PointerCaptureListenerHACK> listener) override {
    FX_LOGS(ERROR) << "CapturePointerEventsHACK is obsolete.";
  }

  // Updates the injection Viewport to match the currently visible display (i.e. accounting for
  // ClipSpaceTransform).
  void UpdateViewport();

  // |fuchsia::accessibility::MagnificationHandler|
  // Sets the transform for screen magnification, applied after the camera projection.
  void SetClipSpaceTransform(float x, float y, float scale,
                             SetClipSpaceTransformCallback callback) override;

  // Nulls out the clip-space transform.
  void ResetClipSpaceTransform();

  // Sets properties for the a11y, proxy, and client view holders.
  void SetViewHolderProperties();

  // |fuchsia::ui::accessibility::view::Registry|
  // This method creates the following scene topology:
  //
  // [1] = owned by root presenter, [2] = owned by client, [3] owned by a11y manager
  //
  // [1] scene
  //       |
  // [1] root view holder
  //       |
  // [1] root view
  //       |
  // [1] injector view holder
  //       |
  // [1] injector view
  //       |
  // [1] a11y view holder
  //       |
  // [3] a11y view
  //       |
  // [3] new proxy view holder
  //       |
  // [1] new proxy view
  //       |
  // [1] client view holder
  //       |
  // [2] client view
  void CreateAccessibilityViewHolder(fuchsia::ui::views::ViewRef a11y_view_ref,
                                     fuchsia::ui::views::ViewHolderToken a11y_view_holder_token,
                                     CreateAccessibilityViewHolderCallback callback) override;

  // Sets |display_metrics_| and updates Scenic.  Returns false if the updates
  // were skipped (if display initialization hasn't happened yet).
  bool ApplyDisplayModelChanges(bool print_log, bool present_changes);
  bool ApplyDisplayModelChangesHelper(bool print_log);

  void InitializeDisplayModel(fuchsia::ui::gfx::DisplayInfo display_info);

  void OnEvent(fuchsia::ui::input::InputEvent event);
  void OnSensorEvent(uint32_t device_id, fuchsia::ui::input::InputReport event);

  // Passes the display rotation in degrees down to the scenic compositor.
  void SetScenicDisplayRotation();

  inspect::Node inspect_node_;
  InputReportInspector input_report_inspector_;
  InputEventInspector input_event_inspector_;

  fuchsia::ui::scenic::Scenic* const scenic_;
  scenic::Session* const session_;
  scenic::ResourceId compositor_id_;

  scenic::Layer layer_;
  scenic::Renderer renderer_;

  // Scene topology:
  // [1] = owned by root presenter, [2] = owned by client
  //
  // [1] scene
  //       |
  // [1] root view holder
  //       |
  // [1] root view
  //       |
  // [1] injector view holder
  //       |
  // [1] injector view view
  //       |
  // [1] proxy view holder
  //       |
  // [1] proxy view
  //       |
  // [1] client view holder
  //       |
  // [2] client view
  //
  // NOTE: This topology changes once the a11y manager connects and calls
  // CreateAccessibilityViewHolder().

  // TODO(fxbug.dev/23500): put camera before scene.
  scenic::Scene scene_;
  scenic::Camera camera_;
  std::optional<scenic::View> root_view_;
  std::optional<scenic::ViewHolder> root_view_holder_;
  fuchsia::ui::views::ViewRef root_view_ref_;

  // The injector view is used as a constant target when injecting events through
  // fuchsia::ui::pointerinjector. When a11y starts, it can insert its view between
  // |injector_view_| and |proxy_view_holder_|.
  scenic::Session injector_session_;
  std::optional<scenic::View> injector_view_;
  std::optional<scenic::ViewHolder> injector_view_holder_;

  // The a11y manager is responsible for hooking its own view up to the scene
  // graph, and attaching the client view below it. The proxy view is necessary
  // to ensure that (1) the a11y manager can insert itself without disrupting
  // the client view, and (2) the client view is rendered and receives input if
  // the a11y manager does not attach its view.
  //
  // |proxy_view_holder_| uses:
  // - It's used to set scale, rotation and translation for all child views.
  // - It's kept in sync with the client view for their ViewProperties.
  // - It is used as the target for fuchsia::ui::pointerinjector to make transforms simpler.
  scenic::Session proxy_session_;
  std::optional<scenic::View> proxy_view_;
  std::optional<scenic::ViewHolder> proxy_view_holder_;

  std::optional<scenic::ViewHolder> client_view_holder_;

  // A11y-specific resources.
  std::optional<scenic::ViewHolder> a11y_view_holder_;

  // True if the proxy view exists as a descendant of the scene root.
  bool proxy_view_attached_to_scene_ = false;

  // True if the client has connected its view.
  bool client_view_connected_to_viewholder_ = false;

  std::optional<InjectorConfigSetup> injector_config_setup_;
  std::optional<Injector> injector_;

  bool display_model_initialized_ = false;
  bool scene_initialized_ = false;

  DisplayModel display_model_;

  // When |display_model_| or |display_rotation_desired_| changes:
  //   * |display_metrics_| must be recalculated.
  //   * |display_rotation_current_| must be updated.
  //   * Transforms on the scene must be updated.
  // This is done by calling ApplyDisplayModelChanges().
  DisplayMetrics display_metrics_;

  // At startup, apply a rotation defined in 90 degree increments, just once.
  // Implies resizing of the presentation to adjust to rotated coordinates.
  // Valid values are ... -180, -90, 0, 90, 180, ...
  //
  // Used when the native display orientation is reported incorrectly.
  // TODO(fxbug.dev/24074) - Make this less of a hack.
  const int32_t display_startup_rotation_adjustment_;

  // Current ClipSpaceTransform. Used to set up a matching input Viewport.
  float clip_scale_ = 1;
  float clip_offset_x_ = 0;
  float clip_offset_y_ = 0;

  fidl::Binding<fuchsia::ui::policy::Presentation> presentation_binding_;
  fidl::Binding<fuchsia::accessibility::MagnificationHandler> a11y_binding_;
  fidl::Binding<fuchsia::ui::accessibility::view::Registry> a11y_view_registry_binding_;

  std::map<uint32_t, std::pair<ui_input::InputDeviceImpl*, std::unique_ptr<ui_input::DeviceState>>>
      device_states_by_id_;

  // |safe_presenter_| is passed in at construction together with the root session.
  SafePresenter* safe_presenter_ = nullptr;

  // |safe_presenter_injector_| is internal and created for |injector_session_|.
  SafePresenter safe_presenter_injector_;

  // |safe_presenter_proxy_| is internal and created for |proxy_session_|.
  SafePresenter safe_presenter_proxy_;

  fxl::WeakPtrFactory<Presentation> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Presentation);
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_PRESENTATION_H_
