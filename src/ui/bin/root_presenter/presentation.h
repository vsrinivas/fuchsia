// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_PRESENTATION_H_
#define SRC_UI_BIN_ROOT_PRESENTER_PRESENTATION_H_

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on
#include <glm/ext.hpp>

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/input2/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/shortcut/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fit/function.h>
#include <lib/ui/scenic/cpp/id.h>
#include <lib/ui/scenic/cpp/resources.h>

#include <map>
#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/ui/input/device_state.h"
#include "src/lib/ui/input/input_device_impl.h"
#include "src/ui/bin/root_presenter/activity_notifier.h"
#include "src/ui/bin/root_presenter/displays/display_metrics.h"
#include "src/ui/bin/root_presenter/displays/display_model.h"
#include "src/ui/bin/root_presenter/media_buttons_handler.h"
#include "src/ui/bin/root_presenter/perspective_demo_mode.h"
#include "src/ui/bin/root_presenter/presentation.h"
#include "src/ui/bin/root_presenter/presentation_switcher.h"
#include "src/ui/bin/root_presenter/renderer_params.h"

namespace root_presenter {

// This class creates a root ViewHolder and sets up rendering of a new scene to
// display the graphical content of the view passed to |PresentScene()|.  It
// also wires up input dispatch and manages the mouse cursor.
//
// The root ViewHolder has the presented (content) view as its child.
//
// The scene's node tree has the following structure:
// + Scene
//   + RootNode
//     + ViewHolder
//       + link: Content view's actual content
//   + child: cursor 1
//   + child: cursor N
//
class Presentation : fuchsia::ui::policy::Presentation,
                     fuchsia::accessibility::MagnificationHandler {
 public:
  // Callback when the presentation yields to the next/previous one.
  using YieldCallback = fit::function<void(bool yield_to_next)>;

  Presentation(fuchsia::ui::scenic::Scenic* scenic, scenic::Session* session,
               scenic::ResourceId compositor_id,
               fuchsia::ui::views::ViewHolderToken view_holder_token,
               fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request,
               fuchsia::ui::shortcut::Manager* shortcut_manager,
               fuchsia::ui::input::ImeService* ime_service, ActivityNotifier* activity_notifier,
               RendererParams renderer_params, int32_t display_startup_rotation_adjustment,
               YieldCallback yield_callback, MediaButtonsHandler* media_buttons_handler);
  ~Presentation() override;

  void RegisterWithMagnifier(fuchsia::accessibility::Magnifier* magnifier);

  void OnReport(uint32_t device_id, fuchsia::ui::input::InputReport report);
  void OnDeviceAdded(ui_input::InputDeviceImpl* input_device);
  void OnDeviceRemoved(uint32_t device_id);

  // Used internally by Presenter. Allows overriding of renderer params.
  void OverrideRendererParams(RendererParams renderer_params, bool present_changes = true);

  // Used internally by Presenter. Reset shortcut manager in case of error.
  void ResetShortcutManager();

  const scenic::Layer& layer() const { return layer_; }
  const scenic::ViewHolder& view_holder() const { return view_holder_; }

 private:
  enum SessionPresentState { kNoPresentPending, kPresentPending, kPresentPendingAndSceneDirty };
  friend class PerspectiveDemoMode;
  friend class PresentationSwitcher;

  // |fuchsia::ui::policy::Presentation|
  void SetRendererParams(std::vector<fuchsia::ui::gfx::RendererParam> params);
  void CaptureKeyboardEventHACK(
      fuchsia::ui::input::KeyboardEvent event_to_capture,
      fidl::InterfaceHandle<fuchsia::ui::policy::KeyboardCaptureListenerHACK> listener) override;
  void CapturePointerEventsHACK(
      fidl::InterfaceHandle<fuchsia::ui::policy::PointerCaptureListenerHACK> listener) override;
  void InjectPointerEventHACK(fuchsia::ui::input::PointerEvent event) override;

  // |fuchsia::accessibility::MagnificationHandler|
  // Sets the transform for screen magnification, applied after the camera projection.
  void SetClipSpaceTransform(float x, float y, float scale,
                             SetClipSpaceTransformCallback callback) override;

  // Nulls out the clip-space transform.
  void ResetClipSpaceTransform();

  // Transforms a device coordinate to a post-magnification coordinate by applying the inverse
  // clip-space transform. This is used to render cursors at the correct location.
  glm::vec2 ApplyInverseClipSpaceTransform(const glm::vec2& coordinate);

  // Sets |display_metrics_| and updates Scenic.  Returns false if the updates
  // were skipped (if display initialization hasn't happened yet).
  bool ApplyDisplayModelChanges(bool print_log, bool present_changes);
  bool ApplyDisplayModelChangesHelper(bool print_log);

  // Returns the raw pointer coordinates transformed by the current display
  // rotation.
  glm::vec2 RotatePointerCoordinates(float x, float y);

  void InitializeDisplayModel(fuchsia::ui::gfx::DisplayInfo display_info);

  // Returns true if the event was consumed and the scene is to be invalidated.
  bool GlobalHooksHandleEvent(const fuchsia::ui::input::InputEvent& event);

  void OnEvent(fuchsia::ui::input::InputEvent event);
  void OnSensorEvent(uint32_t device_id, fuchsia::ui::input::InputReport event);

  // When no shadows, ambient light needs to be full brightness.  Otherwise,
  // ambient needs to be dimmed so that other lights don't "overbrighten".
  void UpdateLightsForShadowTechnique(fuchsia::ui::gfx::ShadowTechnique tech);

  // Set a single RendererParam, unless this value is overridden.
  void SetRendererParam(fuchsia::ui::gfx::RendererParam param);

  // Passes the display rotation in degrees down to the scenic compositor.
  void SetScenicDisplayRotation();

  void PresentScene();

  fuchsia::ui::scenic::Scenic* const scenic_;
  scenic::Session* const session_;
  scenic::ResourceId compositor_id_;
  // Today, a DeviceState is owned by each Presentation, and we need to
  // connect the output of DeviceState to shortcut_manager_.
  fuchsia::ui::shortcut::Manager* shortcut_manager_;
  fuchsia::ui::input::ImeService* ime_service_;
  ActivityNotifier* activity_notifier_;

  scenic::Layer layer_;
  scenic::Renderer renderer_;
  // TODO(SCN-254): put camera before scene.
  scenic::Scene scene_;
  scenic::Camera camera_;
  scenic::AmbientLight ambient_light_;
  scenic::DirectionalLight directional_light_;
  scenic::PointLight point_light_;
  scenic::EntityNode view_holder_node_;
  scenic::EntityNode root_node_;
  scenic::ViewHolder view_holder_;

  scenic::RoundedRectangle cursor_shape_;
  scenic::Material cursor_material_;

  SessionPresentState session_present_state_ = kNoPresentPending;

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

  struct {
    glm::vec2 translation;
    float scale = 1;
  } clip_space_transform_;

  YieldCallback yield_callback_;

  fuchsia::math::PointF mouse_coordinates_;

  fidl::Binding<fuchsia::ui::policy::Presentation> presentation_binding_;
  fidl::Binding<fuchsia::accessibility::MagnificationHandler> a11y_binding_;

  PerspectiveDemoMode perspective_demo_mode_;

  // Toggles through different presentations.
  PresentationSwitcher presentation_switcher_;

  // Stores values that, if set, override any renderer params.
  bool presentation_clipping_enabled_ = true;
  RendererParams renderer_params_override_;

  struct CursorState {
    bool created;
    bool visible;
    fuchsia::math::PointF position;
    std::unique_ptr<scenic::ShapeNode> node;
  };

  std::map<uint32_t, CursorState> cursors_;
  std::map<uint32_t, std::pair<ui_input::InputDeviceImpl*, std::unique_ptr<ui_input::DeviceState>>>
      device_states_by_id_;

  // A registry of listeners who want to be notified when their keyboard
  // event happens.
  struct KeyboardCaptureItem {
    fuchsia::ui::input::KeyboardEvent event;
    fuchsia::ui::policy::KeyboardCaptureListenerHACKPtr listener;
  };
  std::vector<KeyboardCaptureItem> captured_keybindings_;

  // A registry of listeners who want to be notified when pointer event happens.
  fidl::InterfacePtrSet<fuchsia::ui::policy::PointerCaptureListenerHACK> captured_pointerbindings_;

  MediaButtonsHandler* media_buttons_handler_ = nullptr;

  fxl::WeakPtrFactory<Presentation> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Presentation);
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_PRESENTATION_H_
