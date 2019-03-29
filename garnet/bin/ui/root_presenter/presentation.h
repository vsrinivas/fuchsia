// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_

#include <map>
#include <memory>

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/memory/weak_ptr.h>
#include <lib/ui/input/device_state.h>
#include <lib/ui/input/input_device_impl.h>
#include <lib/ui/scenic/cpp/id.h>
#include <lib/ui/scenic/cpp/resources.h>

#include "garnet/bin/ui/presentation_mode/detector.h"
#include "garnet/bin/ui/root_presenter/display_rotater.h"
#include "garnet/bin/ui/root_presenter/display_size_switcher.h"
#include "garnet/bin/ui/root_presenter/display_usage_switcher.h"
#include "garnet/bin/ui/root_presenter/displays/display_metrics.h"
#include "garnet/bin/ui/root_presenter/displays/display_model.h"
#include "garnet/bin/ui/root_presenter/perspective_demo_mode.h"
#include "garnet/bin/ui/root_presenter/presentation.h"
#include "garnet/bin/ui/root_presenter/presentation_switcher.h"
#include "garnet/bin/ui/root_presenter/renderer_params.h"

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
class Presentation : protected fuchsia::ui::policy::Presentation {
 public:
  // Callback when the presentation yields to the next/previous one.
  using YieldCallback = fit::function<void(bool yield_to_next)>;

  Presentation(fuchsia::ui::scenic::Scenic* scenic, scenic::Session* session,
               scenic::ResourceId compositor_id,
               fuchsia::ui::views::ViewHolderToken view_holder_token,
               fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                   presentation_request,
               RendererParams renderer_params,
               int32_t display_startup_rotation_adjustment,
               YieldCallback yield_callback);
  ~Presentation();

  void OnReport(uint32_t device_id, fuchsia::ui::input::InputReport report);
  void OnDeviceAdded(mozart::InputDeviceImpl* input_device);
  void OnDeviceRemoved(uint32_t device_id);

  // Used internally by Presenter. Allows overriding of renderer params.
  void OverrideRendererParams(RendererParams renderer_params,
                              bool present_changes = true);

  const scenic::Layer& layer() const { return layer_; }
  const scenic::ViewHolder& view_holder() const { return view_holder_; }

 private:
  enum SessionPresentState {
    kNoPresentPending,
    kPresentPending,
    kPresentPendingAndSceneDirty
  };
  friend class DisplayRotater;
  friend class DisplayUsageSwitcher;
  friend class PerspectiveDemoMode;
  friend class DisplaySizeSwitcher;
  friend class PresentationSwitcher;

  // |Presentation|
  void EnableClipping(bool enabled) override;
  void UseOrthographicView() override;
  void UsePerspectiveView() override;
  void SetRendererParams(
      ::std::vector<fuchsia::ui::gfx::RendererParam> params) override;
  void SetDisplayUsage(fuchsia::ui::policy::DisplayUsage usage) override;
  void SetDisplaySizeInMm(float width_in_mm, float height_in_mm) override;
  void SetDisplayRotation(float display_rotation_degrees,
                          bool animate) override;
  void CaptureKeyboardEventHACK(
      fuchsia::ui::input::KeyboardEvent event_to_capture,
      fidl::InterfaceHandle<fuchsia::ui::policy::KeyboardCaptureListenerHACK>
          listener) override;
  void CapturePointerEventsHACK(
      fidl::InterfaceHandle<fuchsia::ui::policy::PointerCaptureListenerHACK>
          listener) override;
  void GetPresentationMode(GetPresentationModeCallback callback) override;
  void SetPresentationModeListener(
      fidl::InterfaceHandle<fuchsia::ui::policy::PresentationModeListener>
          listener) override;
  void RegisterMediaButtonsListener(
      fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener)
      override;

  // Sets |display_metrics_| and updates view_manager and Scenic.
  // Returns false if the updates were skipped (if display initialization hasn't
  // happened yet).
  bool ApplyDisplayModelChanges(bool print_log, bool present_changes);
  bool ApplyDisplayModelChangesHelper(bool print_log);

  void InitializeDisplayModel(fuchsia::ui::gfx::DisplayInfo display_info);

  void SetDisplayUsageWithoutApplyingChanges(
      fuchsia::ui::policy::DisplayUsage usage_);

  // Returns false if the operation failed (e.g. the requested size is bigger
  // than the actual display size).
  bool SetDisplaySizeInMmWithoutApplyingChanges(float width_in_mm,
                                                float height_in_mm,
                                                bool print_errors);

  // Returns true if the event was consumed and the scene is to be invalidated.
  bool GlobalHooksHandleEvent(const fuchsia::ui::input::InputEvent& event);

  void OnEvent(fuchsia::ui::input::InputEvent event);
  void OnSensorEvent(uint32_t device_id, fuchsia::ui::input::InputReport event);
  void OnMediaButtonsEvent(fuchsia::ui::input::InputReport event);

  // When no shadows, ambient light needs to be full brightness.  Otherwise,
  // ambient needs to be dimmed so that other lights don't "overbrighten".
  void UpdateLightsForShadowTechnique(fuchsia::ui::gfx::ShadowTechnique tech);

  // Set a single RendererParam, unless this value is overridden.
  void SetRendererParam(fuchsia::ui::gfx::RendererParam param);

  void PresentScene();

  fuchsia::ui::scenic::Scenic* const scenic_;
  scenic::Session* const session_;
  scenic::ResourceId compositor_id_;

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

  DisplayModel display_model_actual_;
  DisplayModel display_model_simulated_;

  // When |display_model_simulated_| or |display_rotation_desired_| changes:
  //   * |display_metrics_| must be recalculated.
  //   * |display_rotation_current_| must be updated.
  //   * Transforms on the scene must be updated.
  // This is done by calling ApplyDisplayModelChanges().
  DisplayMetrics display_metrics_;

  // Expressed in degrees.
  float display_rotation_desired_ = 0.f;
  float display_rotation_current_ = 0.f;

  // At startup, apply a rotation defined in 90 degree increments, just once.
  // Implies resizing of the presentation to adjust to rotated coordinates.
  // Valid values are ... -180, -90, 0, 90, 180, ...
  //
  // Used when the native display orientation is reported incorrectly.
  // TODO(SCN-857) - Make this less of a hack.
  int32_t display_startup_rotation_adjustment_;

  YieldCallback yield_callback_;

  fuchsia::math::PointF mouse_coordinates_;

  fidl::Binding<fuchsia::ui::policy::Presentation> presentation_binding_;

  // Rotates the display 180 degrees in response to events.
  DisplayRotater display_rotater_;

  // Toggles through different display usage values.
  DisplayUsageSwitcher display_usage_switcher_;

  PerspectiveDemoMode perspective_demo_mode_;

  // Toggles through different display sizes.
  DisplaySizeSwitcher display_size_switcher_;

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
  std::map<uint32_t, std::pair<mozart::InputDeviceImpl*,
                               std::unique_ptr<mozart::DeviceState>>>
      device_states_by_id_;

  // A registry of listeners who want to be notified when their keyboard
  // event happens.
  struct KeyboardCaptureItem {
    fuchsia::ui::input::KeyboardEvent event;
    fuchsia::ui::policy::KeyboardCaptureListenerHACKPtr listener;
  };
  std::vector<KeyboardCaptureItem> captured_keybindings_;

  // A registry of listeners who want to be notified when pointer event happens.
  struct PointerCaptureItem {
    fuchsia::ui::policy::PointerCaptureListenerHACKPtr listener;
  };
  std::vector<PointerCaptureItem> captured_pointerbindings_;

  // Listener for changes in presentation mode.
  fuchsia::ui::policy::PresentationModeListenerPtr presentation_mode_listener_;
  // Presentation mode, based on last N measurements
  fuchsia::ui::policy::PresentationMode presentation_mode_;
  std::unique_ptr<presentation_mode::Detector> presentation_mode_detector_;

  // A registry of listeners for media button events.
  std::vector<fuchsia::ui::policy::MediaButtonsListenerPtr>
      media_buttons_listeners_;

  fxl::WeakPtrFactory<Presentation> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Presentation);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_
