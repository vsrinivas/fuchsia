// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/presentation.h"

#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>

#include <cmath>
#include <utility>

#include <trace/event.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/bin/root_presenter/displays/display_configuration.h"
#include "src/ui/lib/key_util/key_util.h"

using fuchsia::ui::policy::MediaButtonsListenerPtr;

namespace root_presenter {
namespace {

// The shape and elevation of the cursor.
constexpr float kCursorWidth = 20;
constexpr float kCursorHeight = 20;
constexpr float kCursorRadius = 10;
// TODO(SCN-1276): Don't hardcode Z bounds in multiple locations.
// Derive cursor elevation from non-hardcoded Z bounds.
constexpr float kCursorElevation = 800;
constexpr float kDefaultRootViewDepth = 1000;

// TODO(SCN-1278): Remove this.
// Turn two floats (high bits, low bits) into a 64-bit uint.
trace_flow_id_t PointerTraceHACK(float fa, float fb) {
  uint32_t ia, ib;
  memcpy(&ia, &fa, sizeof(uint32_t));
  memcpy(&ib, &fb, sizeof(uint32_t));
  return (((uint64_t)ia) << 32) | ib;
}

// Light intensities.
constexpr float kAmbient = 0.3f;
constexpr float kNonAmbient = 1.f - kAmbient;

// Applies the inverse of the given translation in a dimension of Vulkan NDC and scale (about the
// center of the range) to the given coordinate, for inverting the clip-space transform for pointer
// input.
float InverseLinearTransform(float x, uint32_t range, float ndc_translation, float scale) {
  const float half_range = range / 2.f;
  return (x - half_range * (1 + ndc_translation)) / scale + half_range;
}

}  // namespace

Presentation::Presentation(
    fuchsia::ui::scenic::Scenic* scenic, scenic::Session* session, scenic::ResourceId compositor_id,
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request,
    fuchsia::ui::shortcut::Manager* shortcut_manager, fuchsia::ui::input::ImeService* ime_service,
    ActivityNotifier* activity_notifier, RendererParams renderer_params,
    int32_t display_startup_rotation_adjustment, YieldCallback yield_callback,
    MediaButtonsHandler* media_buttons_handler)
    : scenic_(scenic),
      session_(session),
      compositor_id_(compositor_id),
      shortcut_manager_(shortcut_manager),
      ime_service_(ime_service),
      activity_notifier_(activity_notifier),
      layer_(session_),
      renderer_(session_),
      scene_(session_),
      camera_(scene_),
      ambient_light_(session_),
      directional_light_(session_),
      point_light_(session_),
      view_holder_node_(session),
      root_node_(session_),
      view_holder_(session, std::move(view_holder_token), "root_presenter"),
      cursor_shape_(session_, kCursorWidth, kCursorHeight, 0u, kCursorRadius, kCursorRadius,
                    kCursorRadius),
      cursor_material_(session_),
      display_startup_rotation_adjustment_(display_startup_rotation_adjustment),
      yield_callback_(std::move(yield_callback)),
      presentation_binding_(this),
      a11y_binding_(this),
      renderer_params_override_(renderer_params),
      media_buttons_handler_(media_buttons_handler),
      weak_factory_(this) {
  FXL_DCHECK(compositor_id != 0);
  FXL_DCHECK(media_buttons_handler_);
  renderer_.SetCamera(camera_);
  layer_.SetRenderer(renderer_);
  scene_.AddChild(root_node_);
  root_node_.SetTranslation(0.f, 0.f, -0.1f);  // TODO(SCN-371).
  root_node_.AddChild(view_holder_node_);
  view_holder_node_.Attach(view_holder_);

  // Create the root view's scene.
  // TODO(SCN-1255): we add a directional light and a point light, expecting
  // only one of them to be active at a time.  This logic is implicit in
  // EngineRenderer, since no shadow-mode supports both directional and point
  // lights (either one or the other).  When directional light support is added
  // to PaperRenderer, the code here will result in over-brightening, and will
  // need to be adjusted at that time.
  scene_.AddLight(ambient_light_);
  scene_.AddLight(directional_light_);
  scene_.AddLight(point_light_);
  directional_light_.SetDirection(1.f, 1.f, 2.f);
  point_light_.SetPosition(300.f, 300.f, -2000.f);
  point_light_.SetFalloff(0.f);

  // Explicitly set "UNSHADOWED" as the default shadow type. In addition to
  // setting the param, this sets appropriate light intensities.
  {
    fuchsia::ui::gfx::RendererParam param;
    param.set_shadow_technique(fuchsia::ui::gfx::ShadowTechnique::UNSHADOWED);
    SetRendererParam(std::move(param));
  }

  cursor_material_.SetColor(0xff, 0x00, 0xff, 0xff);

  SetScenicDisplayRotation();

  // NOTE: This invokes Present(); all initial scene setup should happen before.
  OverrideRendererParams(renderer_params, false);

  // Link ourselves to the presentation interface once screen dimensions are
  // available for us to present into.
  scenic_->GetDisplayInfo(
      [weak = weak_factory_.GetWeakPtr(), presentation_request = std::move(presentation_request)](
          fuchsia::ui::gfx::DisplayInfo display_info) mutable {
        if (weak) {
          if (presentation_request) {
            weak->presentation_binding_.Bind(std::move(presentation_request));
          }

          // Get display parameters and propagate values appropriately.
          weak->InitializeDisplayModel(std::move(display_info));

          weak->PresentScene();
        }
      });
}

Presentation::~Presentation() = default;

void Presentation::RegisterWithMagnifier(fuchsia::accessibility::Magnifier* magnifier) {
  magnifier->RegisterHandler(a11y_binding_.NewBinding());
  a11y_binding_.set_error_handler([this](auto) { ResetClipSpaceTransform(); });
}

void Presentation::ResetShortcutManager() { shortcut_manager_ = nullptr; }

void Presentation::OverrideRendererParams(RendererParams renderer_params, bool present_changes) {
  renderer_params_override_ = renderer_params;

  if (renderer_params_override_.clipping_enabled.has_value()) {
    presentation_clipping_enabled_ = renderer_params_override_.clipping_enabled.value();
  }
  if (renderer_params_override_.render_frequency.has_value()) {
    fuchsia::ui::gfx::RendererParam param;
    param.set_render_frequency(renderer_params_override_.render_frequency.value());
    renderer_.SetParam(std::move(param));
  }
  if (renderer_params_override_.shadow_technique.has_value()) {
    fuchsia::ui::gfx::RendererParam param;
    param.set_shadow_technique(renderer_params_override_.shadow_technique.value());
    renderer_.SetParam(std::move(param));

    UpdateLightsForShadowTechnique(renderer_params_override_.shadow_technique.value());
  }
  if (present_changes) {
    PresentScene();
  }

  FXL_CHECK(display_startup_rotation_adjustment_ % 90 == 0)
      << "Rotation adjustments must be in (+/-) 90 deg increments; received: "
      << display_startup_rotation_adjustment_;
}

void Presentation::InitializeDisplayModel(fuchsia::ui::gfx::DisplayInfo display_info) {
  FXL_DCHECK(!display_model_initialized_);

  // Initialize display model.
  display_configuration::InitializeModelForDisplay(display_info.width_in_px,
                                                   display_info.height_in_px, &display_model_);

  display_model_initialized_ = true;

  ApplyDisplayModelChanges(true, false);
}

bool Presentation::ApplyDisplayModelChanges(bool print_log, bool present_changes) {
  bool updated = ApplyDisplayModelChangesHelper(print_log);

  if (updated && present_changes) {
    PresentScene();
  }
  return updated;
}

bool Presentation::ApplyDisplayModelChangesHelper(bool print_log) {
  if (!display_model_initialized_)
    return false;

  DisplayMetrics metrics = display_model_.GetMetrics();

  if (print_log) {
    display_configuration::LogDisplayMetrics(metrics);
  }

  if (display_metrics_ == metrics)
    return true;

  display_metrics_ = metrics;

  // Layout size
  {
    float metrics_width = display_metrics_.width_in_pp();
    float metrics_height = display_metrics_.height_in_pp();

    // Swap metrics on left/right tilt.
    if (abs(display_startup_rotation_adjustment_ % 180) == 90) {
      std::swap(metrics_width, metrics_height);
    }

    view_holder_.SetViewProperties(0.f, 0.f, -kDefaultRootViewDepth, metrics_width, metrics_height,
                                   0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    FXL_VLOG(2) << "DisplayModel layout: " << metrics_width << ", " << metrics_height;
  }

  // Device pixel scale.
  {
    float metrics_scale_x = display_metrics_.x_scale_in_px_per_pp();
    float metrics_scale_y = display_metrics_.y_scale_in_px_per_pp();

    // Swap metrics on left/right tilt.
    if (abs(display_startup_rotation_adjustment_ % 180) == 90) {
      std::swap(metrics_scale_x, metrics_scale_y);
    }

    scene_.SetScale(metrics_scale_x, metrics_scale_y, 1.f);
    FXL_VLOG(2) << "DisplayModel pixel scale: " << metrics_scale_x << ", " << metrics_scale_y;
  }

  // Anchor
  {
    float anchor_x = display_metrics_.width_in_pp() / 2;
    float anchor_y = display_metrics_.height_in_pp() / 2;

    // Swap anchors on left/right tilt.
    if (abs(display_startup_rotation_adjustment_ % 180) == 90) {
      std::swap(anchor_x, anchor_y);
    }

    view_holder_node_.SetAnchor(anchor_x, anchor_y, 0);
    FXL_VLOG(2) << "DisplayModel anchor: " << anchor_x << ", " << anchor_y;
  }

  // Rotate
  {
    glm::quat display_rotation =
        glm::quat(glm::vec3(0, 0, glm::radians<float>(display_startup_rotation_adjustment_)));
    view_holder_node_.SetRotation(display_rotation.x, display_rotation.y, display_rotation.z,
                                  display_rotation.w);
  }

  const DisplayModel::DisplayInfo& display_info = display_model_.display_info();

  // Center everything.
  {
    float info_w = display_info.width_in_px;
    float info_h = display_info.height_in_px;
    float metrics_w = display_metrics_.width_in_px();
    float metrics_h = display_metrics_.height_in_px();
    float density_w = display_metrics_.x_scale_in_px_per_pp();
    float density_h = display_metrics_.y_scale_in_px_per_pp();

    // Swap metrics on left/right tilt.
    if (abs(display_startup_rotation_adjustment_ % 180) == 90) {
      std::swap(metrics_w, metrics_h);
      std::swap(density_w, density_h);
    }

    float left_offset = (info_w - metrics_w) / density_w / 2;
    float top_offset = (info_h - metrics_h) / density_h / 2;

    view_holder_node_.SetTranslation(left_offset, top_offset, 0.f);
    FXL_VLOG(2) << "DisplayModel translation: " << left_offset << ", " << top_offset;
  }

  // Today, a layer needs the display's physical dimensions to render correctly.
  layer_.SetSize(static_cast<float>(display_info.width_in_px),
                 static_cast<float>(display_info.height_in_px));

  return true;
}

glm::vec2 Presentation::RotatePointerCoordinates(float x, float y) {
  // TODO(SCN-911): This math is messy and hard to understand. Instead, we
  // should just walk down the layer and scene graph and apply transformations.
  // On the other hand, this method is only used when capturing touch events,
  // which is something we intend to deprecate anyway.

  const float anchor_w = display_model_.display_info().width_in_px / 2;
  const float anchor_h = display_model_.display_info().height_in_px / 2;
  const int32_t startup_rotation = display_startup_rotation_adjustment_;

  glm::vec4 pointer_coords(x, y, 0.f, 1.f);
  glm::vec4 rotated_coords =
      glm::translate(glm::vec3(anchor_w, anchor_h, 0)) *
      glm::rotate(glm::radians<float>(-startup_rotation), glm::vec3(0, 0, 1)) *
      glm::translate(glm::vec3(-anchor_w, -anchor_h, 0)) * pointer_coords;

  if (abs(startup_rotation) % 180 == 90) {
    // If the aspect ratio is flipped, the origin needs to be adjusted too.
    int32_t sim_w = static_cast<int32_t>(display_metrics_.width_in_px());
    int32_t sim_h = static_cast<int32_t>(display_metrics_.height_in_px());
    float adjust_origin = (sim_w - sim_h) / 2.f;
    rotated_coords = glm::translate(glm::vec3(-adjust_origin, adjust_origin, 0)) * rotated_coords;
  }

  FXL_VLOG(2) << "Pointer coordinates rotated [" << startup_rotation << "]: (" << pointer_coords.x
              << ", " << pointer_coords.y << ")->(" << rotated_coords.x << ", " << rotated_coords.y
              << ").";

  return glm::vec2(rotated_coords.x, rotated_coords.y);
}

void Presentation::OnDeviceAdded(ui_input::InputDeviceImpl* input_device) {
  FXL_VLOG(1) << "OnDeviceAdded: device_id=" << input_device->id();

  FXL_DCHECK(device_states_by_id_.count(input_device->id()) == 0);

  std::unique_ptr<ui_input::DeviceState> state;

  if (input_device->descriptor()->sensor) {
    ui_input::OnSensorEventCallback callback = [this](uint32_t device_id,
                                                      fuchsia::ui::input::InputReport event) {
      OnSensorEvent(device_id, std::move(event));
    };
    state = std::make_unique<ui_input::DeviceState>(input_device->id(), input_device->descriptor(),
                                                    std::move(callback));
  } else {
    ui_input::OnEventCallback callback = [this](fuchsia::ui::input::InputEvent event) {
      OnEvent(std::move(event));
    };
    state = std::make_unique<ui_input::DeviceState>(input_device->id(), input_device->descriptor(),
                                                    std::move(callback));
  }

  ui_input::DeviceState* state_ptr = state.get();
  auto device_pair = std::make_pair(input_device, std::move(state));
  state_ptr->OnRegistered();
  device_states_by_id_.emplace(input_device->id(), std::move(device_pair));
}

void Presentation::OnDeviceRemoved(uint32_t device_id) {
  FXL_VLOG(1) << "OnDeviceRemoved: device_id=" << device_id;

  if (device_states_by_id_.count(device_id) != 0) {
    device_states_by_id_[device_id].second->OnUnregistered();
    auto it = cursors_.find(device_id);
    if (it != cursors_.end()) {
      it->second.node->Detach();
      cursors_.erase(it);
      PresentScene();
    }
    device_states_by_id_.erase(device_id);
  }
}

void Presentation::OnReport(uint32_t device_id, fuchsia::ui::input::InputReport input_report) {
  // Media buttons should be processed by MediaButtonsHandler.
  FXL_DCHECK(!input_report.media_buttons);
  TRACE_DURATION("input", "presentation_on_report", "id", input_report.trace_id);
  TRACE_FLOW_END("input", "report_to_presentation", input_report.trace_id);

  FXL_VLOG(2) << "OnReport device=" << device_id
              << ", count=" << device_states_by_id_.count(device_id) << ", report=" << input_report;

  if (device_states_by_id_.count(device_id) == 0) {
    FXL_VLOG(1) << "OnReport: Unknown device " << device_id;
    return;
  }

  if (!display_model_initialized_)
    return;

  ui_input::DeviceState* state = device_states_by_id_[device_id].second.get();
  fuchsia::math::Size size;
  size.width = display_model_.display_info().width_in_px;
  size.height = display_model_.display_info().height_in_px;

  TRACE_FLOW_BEGIN("input", "report_to_device_state", input_report.trace_id);
  state->Update(std::move(input_report), size);
}

void Presentation::CaptureKeyboardEventHACK(
    fuchsia::ui::input::KeyboardEvent event_to_capture,
    fidl::InterfaceHandle<fuchsia::ui::policy::KeyboardCaptureListenerHACK> listener_handle) {
  fuchsia::ui::policy::KeyboardCaptureListenerHACKPtr listener;
  listener.Bind(std::move(listener_handle));
  // Auto-remove listeners if the interface closes.
  listener.set_error_handler([this, listener = listener.get()](zx_status_t status) {
    captured_keybindings_.erase(
        std::remove_if(captured_keybindings_.begin(), captured_keybindings_.end(),
                       [listener](const KeyboardCaptureItem& item) -> bool {
                         return item.listener.get() == listener;
                       }),
        captured_keybindings_.end());
  });

  captured_keybindings_.push_back(
      KeyboardCaptureItem{std::move(event_to_capture), std::move(listener)});
}

void Presentation::CapturePointerEventsHACK(
    fidl::InterfaceHandle<fuchsia::ui::policy::PointerCaptureListenerHACK> listener_handle) {
  captured_pointerbindings_.AddInterfacePtr(listener_handle.Bind());
}

void Presentation::InjectPointerEventHACK(fuchsia::ui::input::PointerEvent event) {
  fuchsia::ui::input::InputEvent input_event;
  input_event.set_pointer(std::move(event));
  OnEvent(std::move(input_event));
}

void Presentation::SetClipSpaceTransform(float x, float y, float scale,
                                         SetClipSpaceTransformCallback callback) {
  camera_.SetClipSpaceTransform(x, y, scale);
  clip_space_transform_ = {{x, y}, scale};
  // The callback is used to throttle magnification transition animations and is expected to
  // approximate the framerate.
  // TODO(35521): In the future, this may need to be downsampled as |Present| calls must be
  // throttled, at which point the |SetClipSpaceTransformCallback|s should be consolidated.
  session_->Present(0, [callback = std::move(callback)](auto) { callback(); });
}

void Presentation::ResetClipSpaceTransform() {
  SetClipSpaceTransform(0, 0, 1, [] {});
}

glm::vec2 Presentation::ApplyInverseClipSpaceTransform(const glm::vec2& coordinate) {
  return {
      InverseLinearTransform(coordinate.x, display_model_.display_info().width_in_px,
                             clip_space_transform_.translation.x, clip_space_transform_.scale),
      InverseLinearTransform(coordinate.y, display_model_.display_info().height_in_px,
                             clip_space_transform_.translation.y, clip_space_transform_.scale),
  };
}

bool Presentation::GlobalHooksHandleEvent(const fuchsia::ui::input::InputEvent& event) {
  return perspective_demo_mode_.OnEvent(event, this) || presentation_switcher_.OnEvent(event, this);
}

void Presentation::OnEvent(fuchsia::ui::input::InputEvent event) {
  TRACE_DURATION("input", "presentation_on_event");
  trace_flow_id_t trace_id = 0;

  FXL_VLOG(1) << "OnEvent " << event;

  activity_notifier_->ReceiveInputEvent(event);

  fuchsia::ui::input::Command input_cmd;

  bool invalidate = false;
  bool dispatch_event = true;

  if (GlobalHooksHandleEvent(event)) {
    invalidate = true;
    dispatch_event = false;
  }

  // Process the event.
  if (dispatch_event) {
    if (event.is_pointer()) {
      const fuchsia::ui::input::PointerEvent& pointer = event.pointer();

      // TODO(SCN-1278): Use proper trace_id for tracing flow.
      trace_id = PointerTraceHACK(pointer.radius_major, pointer.radius_minor);
      TRACE_FLOW_END("input", "dispatch_event_to_presentation", trace_id);

      // Ensure the cursor appears at the correct position after magnification position and scaling.
      // It should appear at the same physical location on the screen as it would without
      // magnification. (However, the cursor itself will scale.)
      const glm::vec2 transformed_point = ApplyInverseClipSpaceTransform({pointer.x, pointer.y});

      if (pointer.type == fuchsia::ui::input::PointerEventType::MOUSE) {
        if (cursors_.count(pointer.device_id) == 0) {
          cursors_.emplace(pointer.device_id, CursorState{});
        }

        cursors_[pointer.device_id].position.x = transformed_point.x;
        cursors_[pointer.device_id].position.y = transformed_point.y;

        // TODO(SCN-823) for now don't show cursor when mouse is added until
        // we have a timer to hide it. Acer12 sleeve reports 2 mice but only
        // one will generate events for now.
        if (pointer.phase != fuchsia::ui::input::PointerEventPhase::ADD &&
            pointer.phase != fuchsia::ui::input::PointerEventPhase::REMOVE) {
          cursors_[pointer.device_id].visible = true;
        }
        invalidate = true;
      } else {
        for (auto it = cursors_.begin(); it != cursors_.end(); ++it) {
          if (it->second.visible) {
            it->second.visible = false;
            invalidate = true;
          }
        }
      }

      // The following steps are different ways of dispatching pointer events, which differ in their
      // coordinate systems.

      if (!captured_pointerbindings_.ptrs().empty()) {
        // |CapturePointerEventsHACK| clients like SysUI expect rotated, transformed coordinates as
        // this bypasses normal input dispatch and so needs to be pretty much ready-to-use.
        glm::vec2 capture_point =
            RotatePointerCoordinates(transformed_point.x, transformed_point.y);

        // Adjust pointer origin with simulated screen offset.
        capture_point.x -=
            (display_model_.display_info().width_in_px - display_metrics_.width_in_px()) / 2.f;
        capture_point.y -=
            (display_model_.display_info().height_in_px - display_metrics_.height_in_px()) / 2.f;

        // Scale by device pixel density.
        capture_point.x *= display_metrics_.x_scale_in_pp_per_px();
        capture_point.y *= display_metrics_.y_scale_in_pp_per_px();

        for (auto& listener : captured_pointerbindings_.ptrs()) {
          fuchsia::ui::input::PointerEvent clone;
          fidl::Clone(pointer, &clone);
          clone.x = capture_point.x;
          clone.y = capture_point.y;
          (*listener)->OnPointerEvent(std::move(clone));
        }
      }

      fuchsia::ui::input::SendPointerInputCmd pointer_cmd;
      pointer_cmd.pointer_event = std::move(pointer);
      pointer_cmd.compositor_id = compositor_id_;
      input_cmd.set_send_pointer_input(std::move(pointer_cmd));

    } else if (event.is_keyboard()) {
      const fuchsia::ui::input::KeyboardEvent& kbd = event.keyboard();

      // Keyboard uses alternate dispatch path.
      dispatch_event = false;

      auto handled_callback = [this, kbd, input_cmd = std::move(input_cmd),
                               trace_id](bool was_handled) mutable {
        // Unconditionally perform legacy shortcuts.
        // TODO(SCN-1465): deprecate legacy shortcuts.
        for (size_t i = 0; i < captured_keybindings_.size(); i++) {
          const auto& event = captured_keybindings_[i].event;
          if (event.modifiers == kbd.modifiers && event.phase == kbd.phase) {
            if ((event.code_point > 0 && event.code_point == kbd.code_point) ||
                // match on hid_usage when there's no codepoint:
                event.hid_usage == kbd.hid_usage) {
              fuchsia::ui::input::KeyboardEvent clone;
              fidl::Clone(kbd, &clone);
              captured_keybindings_[i].listener->OnEvent(std::move(clone));
              was_handled = true;
            }
          }
        }

        if (!was_handled) {
          if (trace_id) {
            TRACE_FLOW_BEGIN("input", "dispatch_event_to_scenic", trace_id);
          }
          fuchsia::ui::input::SendKeyboardInputCmd keyboard_cmd;
          keyboard_cmd.keyboard_event = std::move(kbd);
          keyboard_cmd.compositor_id = compositor_id_;
          input_cmd.set_send_keyboard_input(std::move(keyboard_cmd));

          // TODO(SCN-1455): Remove Scenic from keyboard dispatch path.
          session_->Enqueue(std::move(input_cmd));
        }
      };

      // [Transitional]
      // Current keyboard input dispatch strategy:
      // For the user-facing keys (fuchsia::ui::input2::Key), events
      // are delivered to services in the following order:
      // 1. ime_service, to provide fuchsia.ui.input2.Keyboard FIDL
      // 2. shortcuts, to provide fuchsia.ui.shortcut FIDL
      //
      // If this flow doesn't handle the event, events are passed to
      // legacy flow (see handled_callback):
      // 1. CaptureKeyboardEventHACK FIDL
      // 2. session->Enqueue
      if (auto key_event = key_util::into_key_event(kbd)) {
        fuchsia::ui::input2::KeyEvent clone;
        fidl::Clone(*key_event, &clone);
        // Send keyboard event to IME manager for input2.KeyboardService
        // interface.
        ime_service_->DispatchKey(
            std::move(*key_event),
            [this, key_event = std::move(clone),
             handled_callback = std::move(handled_callback)](bool was_handled) mutable {
              if (was_handled || !shortcut_manager_) {
                handled_callback(false);
              } else {
                // Send keyboard event to Shortcut manager for shortcut
                // detection.
                shortcut_manager_->HandleKeyEvent(std::move(key_event),
                                                  std::move(handled_callback));
              }
            });
      } else {
        handled_callback(false);
      }
      return;
    }
  }

  if (invalidate) {
    PresentScene();
  }

  if (dispatch_event) {
    if (trace_id) {
      TRACE_FLOW_BEGIN("input", "dispatch_event_to_scenic", trace_id);
    }
    session_->Enqueue(std::move(input_cmd));
  }
}

void Presentation::OnSensorEvent(uint32_t device_id, fuchsia::ui::input::InputReport event) {
  FXL_VLOG(2) << "OnSensorEvent(device_id=" << device_id << "): " << event;

  FXL_DCHECK(device_states_by_id_.count(device_id) > 0);
  FXL_DCHECK(device_states_by_id_[device_id].first);
  FXL_DCHECK(device_states_by_id_[device_id].first->descriptor());
  FXL_DCHECK(device_states_by_id_[device_id].first->descriptor()->sensor.get());

  // No clients of sensor events at the moment.
}

void Presentation::PresentScene() {
  if (session_present_state_ == kPresentPendingAndSceneDirty) {
    return;
  } else if (session_present_state_ == kPresentPending) {
    session_present_state_ = kPresentPendingAndSceneDirty;
    return;
  }

  // There is no present pending, so we will kick one off.
  session_present_state_ = kPresentPending;

  bool use_clipping = perspective_demo_mode_.WantsClipping();
  if (renderer_params_override_.clipping_enabled.has_value()) {
    use_clipping = renderer_params_override_.clipping_enabled.value();
  }
  renderer_.SetDisableClipping(!use_clipping);

  // TODO(SCN-631): Individual Presentations shouldn't directly manage cursor
  // state.
  for (auto it = cursors_.begin(); it != cursors_.end(); ++it) {
    CursorState& state = it->second;
    if (state.visible) {
      if (!state.created) {
        state.node = std::make_unique<scenic::ShapeNode>(session_);
        state.node->SetLabel("mouse cursor");
        state.node->SetShape(cursor_shape_);
        state.node->SetMaterial(cursor_material_);
        scene_.AddChild(*state.node);
        state.created = true;
      }
      state.node->SetTranslation(
          state.position.x * display_metrics_.x_scale_in_pp_per_px() + kCursorWidth * .5f,
          state.position.y * display_metrics_.y_scale_in_pp_per_px() + kCursorHeight * .5f,
          -kCursorElevation);
    } else if (state.created) {
      state.node->Detach();
      state.created = false;
    }
  }

  session_->Present(0, [weak = weak_factory_.GetWeakPtr()](fuchsia::images::PresentationInfo info) {
    if (auto self = weak.get()) {
      uint64_t next_presentation_time = info.presentation_time + info.presentation_interval;

      bool scene_dirty = self->session_present_state_ == kPresentPendingAndSceneDirty;

      // Clear the present state.
      self->session_present_state_ = kNoPresentPending;

      scene_dirty |= self->perspective_demo_mode_.UpdateAnimation(self, next_presentation_time);
      if (scene_dirty) {
        self->PresentScene();
      }
    }
  });
}

void Presentation::UpdateLightsForShadowTechnique(fuchsia::ui::gfx::ShadowTechnique tech) {
  if (tech == fuchsia::ui::gfx::ShadowTechnique::UNSHADOWED) {
    ambient_light_.SetColor(1.f, 1.f, 1.f);
    directional_light_.SetColor(0.f, 0.f, 0.f);
    point_light_.SetColor(0.f, 0.f, 0.f);
  } else {
    ambient_light_.SetColor(kAmbient, kAmbient, kAmbient);
    directional_light_.SetColor(kNonAmbient, kNonAmbient, kNonAmbient);
    point_light_.SetColor(kNonAmbient, kNonAmbient, kNonAmbient);
  }
}

void Presentation::SetRendererParams(std::vector<fuchsia::ui::gfx::RendererParam> params) {
  for (size_t i = 0; i < params.size(); ++i) {
    SetRendererParam(std::move(params[i]));
  }
  session_->Present(0, [](fuchsia::images::PresentationInfo info) {});
}

void Presentation::SetScenicDisplayRotation() {
  fuchsia::ui::gfx::Command command;
  fuchsia::ui::gfx::SetDisplayRotationCmdHACK display_rotation_cmd;
  display_rotation_cmd.compositor_id = compositor_id_;
  display_rotation_cmd.rotation_degrees = display_startup_rotation_adjustment_;
  command.set_set_display_rotation(std::move(display_rotation_cmd));
  session_->Enqueue(std::move(command));
}

void Presentation::SetRendererParam(fuchsia::ui::gfx::RendererParam param) {
  switch (param.Which()) {
    case ::fuchsia::ui::gfx::RendererParam::Tag::kShadowTechnique:
      if (renderer_params_override_.shadow_technique.has_value()) {
        FXL_LOG(WARNING) << "Presentation::SetRendererParams: Cannot change "
                            "shadow technique, default was overriden in root_presenter";
        return;
      }
      UpdateLightsForShadowTechnique(param.shadow_technique());
      break;
    case fuchsia::ui::gfx::RendererParam::Tag::kRenderFrequency:
      if (renderer_params_override_.render_frequency.has_value()) {
        FXL_LOG(WARNING) << "Presentation::SetRendererParams: Cannot change "
                            "render frequency, default was overriden in root_presenter";
        return;
      }
      break;
    case fuchsia::ui::gfx::RendererParam::Tag::kEnableDebugging:
      if (renderer_params_override_.debug_enabled.has_value()) {
        FXL_LOG(WARNING) << "Presentation::SetRendererParams: Cannot change "
                            "debug enabled, default was overriden in root_presenter";
        return;
      }
      break;
    case fuchsia::ui::gfx::RendererParam::Tag::Invalid:
      return;
  }
  renderer_.SetParam(std::move(param));
}

}  // namespace root_presenter
