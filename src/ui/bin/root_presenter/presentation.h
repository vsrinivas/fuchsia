// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_PRESENTATION_H_
#define SRC_UI_BIN_ROOT_PRESENTER_PRESENTATION_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/input2/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/ui/scenic/cpp/id.h>
#include <lib/ui/scenic/cpp/resources.h>

#include <map>

#include "src/lib/ui/input/device_state.h"
#include "src/lib/ui/input/input_device_impl.h"
#include "src/ui/bin/root_presenter/activity_notifier.h"
#include "src/ui/bin/root_presenter/displays/display_metrics.h"
#include "src/ui/bin/root_presenter/displays/display_model.h"
#include "src/ui/bin/root_presenter/presentation.h"
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
                     fuchsia::accessibility::MagnificationHandler {
 public:
  Presentation(fuchsia::ui::scenic::Scenic* scenic, scenic::Session* session,
               scenic::ResourceId compositor_id,
               fuchsia::ui::views::ViewHolderToken view_holder_token,
               fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request,
               SafePresenter* safe_presenter, ActivityNotifier* activity_notifier,
               int32_t display_startup_rotation_adjustment);
  ~Presentation() override;

  void RegisterWithMagnifier(fuchsia::accessibility::Magnifier* magnifier);

  void OnReport(uint32_t device_id, fuchsia::ui::input::InputReport report);
  void OnDeviceAdded(ui_input::InputDeviceImpl* input_device);
  void OnDeviceRemoved(uint32_t device_id);

  const scenic::Layer& layer() const { return layer_; }
  const scenic::ViewHolder& view_holder() const { return view_holder_; }

 private:
  // |fuchsia::ui::policy::Presentation|
  void CapturePointerEventsHACK(
      fidl::InterfaceHandle<fuchsia::ui::policy::PointerCaptureListenerHACK> listener) override {
    FX_LOGS(ERROR) << "CapturePointerEventsHACK is obsolete.";
  }

  // |fuchsia::accessibility::MagnificationHandler|
  // Sets the transform for screen magnification, applied after the camera projection.
  void SetClipSpaceTransform(float x, float y, float scale,
                             SetClipSpaceTransformCallback callback) override;

  // Nulls out the clip-space transform.
  void ResetClipSpaceTransform();

  // Sets |display_metrics_| and updates Scenic.  Returns false if the updates
  // were skipped (if display initialization hasn't happened yet).
  bool ApplyDisplayModelChanges(bool print_log, bool present_changes);
  bool ApplyDisplayModelChangesHelper(bool print_log);

  void InitializeDisplayModel(fuchsia::ui::gfx::DisplayInfo display_info);

  void OnEvent(fuchsia::ui::input::InputEvent event);
  void OnSensorEvent(uint32_t device_id, fuchsia::ui::input::InputReport event);

  // Passes the display rotation in degrees down to the scenic compositor.
  void SetScenicDisplayRotation();

  fuchsia::ui::scenic::Scenic* const scenic_;
  scenic::Session* const session_;
  scenic::ResourceId compositor_id_;

  ActivityNotifier* activity_notifier_;

  scenic::Layer layer_;
  scenic::Renderer renderer_;
  // TODO(SCN-254): put camera before scene.
  scenic::Scene scene_;
  scenic::Camera camera_;
  scenic::EntityNode view_holder_node_;
  scenic::EntityNode root_node_;
  scenic::ViewHolder view_holder_;

  bool display_model_initialized_ = false;

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
  // TODO(SCN-857) - Make this less of a hack.
  int32_t display_startup_rotation_adjustment_;

  fidl::Binding<fuchsia::ui::policy::Presentation> presentation_binding_;
  fidl::Binding<fuchsia::accessibility::MagnificationHandler> a11y_binding_;

  std::map<uint32_t, std::pair<ui_input::InputDeviceImpl*, std::unique_ptr<ui_input::DeviceState>>>
      device_states_by_id_;

  SafePresenter* safe_presenter_ = nullptr;

  fxl::WeakPtrFactory<Presentation> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Presentation);
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_PRESENTATION_H_
