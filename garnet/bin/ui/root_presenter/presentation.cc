// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/presentation.h"

#include <cmath>
#include <utility>

#include <lib/component/cpp/connect.h>
#include <lib/fxl/logging.h>
#include <lib/ui/input/cpp/formatting.h>
#include <trace/event.h>

#include "garnet/bin/ui/root_presenter/displays/display_configuration.h"

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

}  // namespace

Presentation::Presentation(
    fuchsia::ui::scenic::Scenic* scenic, scenic::Session* session,
    scenic::ResourceId compositor_id,
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
        presentation_request,
    RendererParams renderer_params, int32_t display_startup_rotation_adjustment,
    YieldCallback yield_callback)
    : scenic_(scenic),
      session_(session),
      compositor_id_(compositor_id),
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
      cursor_shape_(session_, kCursorWidth, kCursorHeight, 0u, kCursorRadius,
                    kCursorRadius, kCursorRadius),
      cursor_material_(session_),
      display_startup_rotation_adjustment_(display_startup_rotation_adjustment),
      yield_callback_(std::move(yield_callback)),
      presentation_binding_(this),
      renderer_params_override_(renderer_params),
      weak_factory_(this) {
  FXL_DCHECK(compositor_id != 0);
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
  // to PaperRenderer2, the code here will result in over-brightening, and will
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

  // NOTE: This invokes Present(); all initial scene setup should happen before.
  OverrideRendererParams(renderer_params, false);

  // Link ourselves to the presentation interface once screen dimensions are
  // available for us to present into.
  scenic_->GetDisplayInfo(
      [weak = weak_factory_.GetWeakPtr(),
       presentation_request = std::move(presentation_request)](
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

void Presentation::OverrideRendererParams(RendererParams renderer_params,
                                          bool present_changes) {
  renderer_params_override_ = renderer_params;

  if (renderer_params_override_.clipping_enabled.has_value()) {
    presentation_clipping_enabled_ =
        renderer_params_override_.clipping_enabled.value();
  }
  if (renderer_params_override_.render_frequency.has_value()) {
    fuchsia::ui::gfx::RendererParam param;
    param.set_render_frequency(
        renderer_params_override_.render_frequency.value());
    renderer_.SetParam(std::move(param));
  }
  if (renderer_params_override_.shadow_technique.has_value()) {
    fuchsia::ui::gfx::RendererParam param;
    param.set_shadow_technique(
        renderer_params_override_.shadow_technique.value());
    renderer_.SetParam(std::move(param));

    UpdateLightsForShadowTechnique(
        renderer_params_override_.shadow_technique.value());
  }
  if (present_changes) {
    PresentScene();
  }

  FXL_CHECK(display_startup_rotation_adjustment_ % 90 == 0)
      << "Rotation adjustments must be in (+/-) 90 deg increments; received: "
      << display_startup_rotation_adjustment_;
}

Presentation::~Presentation() {}

void Presentation::InitializeDisplayModel(
    fuchsia::ui::gfx::DisplayInfo display_info) {
  FXL_DCHECK(!display_model_initialized_);

  // Save previous display values. These could have been overridden by earlier
  // calls to SetDisplayUsage() and SetDisplaySizeInMm(); if not, they will
  // be unknown or 0.
  auto previous_display_usage =
      display_model_simulated_.environment_info().usage;

  auto previous_display_width_in_mm =
      display_model_simulated_.display_info().width_in_mm;
  auto previous_display_height_in_mm =
      display_model_simulated_.display_info().height_in_mm;

  // Initialize display model.
  display_configuration::InitializeModelForDisplay(display_info.width_in_px,
                                                   display_info.height_in_px,
                                                   &display_model_actual_);
  display_model_simulated_ = display_model_actual_;

  display_model_initialized_ = true;

  // Re-set the model with previous values. If they were unknown or 0, the
  // actual/default values will be used.
  SetDisplayUsageWithoutApplyingChanges(previous_display_usage);
  SetDisplaySizeInMmWithoutApplyingChanges(previous_display_width_in_mm,
                                           previous_display_height_in_mm, true);

  ApplyDisplayModelChanges(true, false);
}

void Presentation::SetDisplaySizeInMm(float width_in_mm, float height_in_mm) {
  uint32_t old_width_in_mm =
      display_model_simulated_.display_info().width_in_mm;
  uint32_t old_height_in_mm =
      display_model_simulated_.display_info().height_in_mm;

  SetDisplaySizeInMmWithoutApplyingChanges(width_in_mm, height_in_mm, true);

  if (display_model_simulated_.display_info().width_in_mm == old_width_in_mm &&
      display_model_simulated_.display_info().height_in_mm ==
          old_height_in_mm) {
    // Nothing needs to be changed.
    return;
  }

  FXL_LOG(INFO) << "Presentation::SetDisplaySizeInMm: changing display "
                   "dimensions to "
                << "width="
                << display_model_simulated_.display_info().width_in_mm << "mm, "
                << "height="
                << display_model_simulated_.display_info().height_in_mm
                << "mm.";

  ApplyDisplayModelChanges(true, true);
}

void Presentation::SetDisplayRotation(float display_rotation_degrees,
                                      bool animate) {
  display_rotater_.SetDisplayRotation(this, display_rotation_degrees, animate);
};

bool Presentation::SetDisplaySizeInMmWithoutApplyingChanges(float width_in_mm,
                                                            float height_in_mm,
                                                            bool print_errors) {
  if (width_in_mm == 0 || height_in_mm == 0) {
    display_model_simulated_.display_info().width_in_px =
        display_model_actual_.display_info().width_in_px;
    display_model_simulated_.display_info().height_in_px =
        display_model_actual_.display_info().height_in_px;
    display_model_simulated_.display_info().width_in_mm =
        display_model_actual_.display_info().width_in_mm;
    display_model_simulated_.display_info().height_in_mm =
        display_model_actual_.display_info().height_in_mm;
    return true;
  }

  const float kPxPerMm =
      display_model_actual_.display_info().density_in_px_per_mm;
  uint32_t width_in_px = width_in_mm * kPxPerMm;
  uint32_t height_in_px = height_in_mm * kPxPerMm;

  if (width_in_px > display_model_actual_.display_info().width_in_px) {
    if (print_errors) {
      FXL_LOG(ERROR) << "Presentation::SetDisplaySizeInMm: tried to change "
                        "display width to "
                     << width_in_mm
                     << ", which is larger than the actual display width "
                     << display_model_actual_.display_info().width_in_px /
                            kPxPerMm;
    }
    return false;
  }
  if (height_in_px > display_model_actual_.display_info().height_in_px) {
    if (print_errors) {
      FXL_LOG(ERROR) << "Presentation::SetDisplaySizeInMm: tried to change "
                        "display height to "
                     << height_in_mm
                     << ", which is larger than the actual display height "
                     << display_model_actual_.display_info().height_in_px /
                            kPxPerMm;
    }
    return false;
  }

  display_model_simulated_.display_info().width_in_px = width_in_px;
  display_model_simulated_.display_info().height_in_px = height_in_px;
  display_model_simulated_.display_info().width_in_mm = width_in_mm;
  display_model_simulated_.display_info().height_in_mm = height_in_mm;
  return true;
}

void Presentation::SetDisplayUsage(fuchsia::ui::policy::DisplayUsage usage) {
  fuchsia::ui::policy::DisplayUsage old_usage =
      display_model_simulated_.environment_info().usage;
  SetDisplayUsageWithoutApplyingChanges(usage);
  if (display_model_simulated_.environment_info().usage == old_usage) {
    // Nothing needs to be changed.
    return;
  }

  ApplyDisplayModelChanges(true, true);

  FXL_LOG(INFO) << "Presentation::SetDisplayUsage: changing display usage to "
                << GetDisplayUsageAsString(
                       display_model_simulated_.environment_info().usage);
}

void Presentation::SetDisplayUsageWithoutApplyingChanges(
    fuchsia::ui::policy::DisplayUsage usage) {
  display_model_simulated_.environment_info().usage =
      (usage == fuchsia::ui::policy::DisplayUsage::kUnknown)
          ? display_model_actual_.environment_info().usage
          : usage;
}

bool Presentation::ApplyDisplayModelChanges(bool print_log,
                                            bool present_changes) {
  bool updated = ApplyDisplayModelChangesHelper(print_log);

  if (updated && present_changes) {
    PresentScene();
  }
  return updated;
}

bool Presentation::ApplyDisplayModelChangesHelper(bool print_log) {
  if (!display_model_initialized_)
    return false;

  DisplayMetrics metrics = display_model_simulated_.GetMetrics();

  if (print_log) {
    display_configuration::LogDisplayMetrics(metrics);
  }

  if (display_metrics_ == metrics &&
      display_rotation_desired_ == display_rotation_current_)
    return true;

  display_metrics_ = metrics;
  display_rotation_current_ = display_rotation_desired_;

  // Layout size
  {
    float metrics_width = display_metrics_.width_in_pp();
    float metrics_height = display_metrics_.height_in_pp();

    // Swap metrics on left/right tilt.
    if (abs(display_startup_rotation_adjustment_ % 180) == 90) {
      std::swap(metrics_width, metrics_height);
    }

    view_holder_.SetViewProperties(0.f, 0.f, -kDefaultRootViewDepth,
                                   metrics_width, metrics_height, 0.f, 0.f, 0.f,
                                   0.f, 0.f, 0.f, 0.f);
    FXL_VLOG(2) << "DisplayModel layout: " << metrics_width << ", "
                << metrics_height;
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
    FXL_VLOG(2) << "DisplayModel pixel scale: " << metrics_scale_x << ", "
                << metrics_scale_y;
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
    glm::quat display_rotation = glm::quat(
        glm::vec3(0, 0,
                  glm::radians<float>(display_rotation_current_ +
                                      display_startup_rotation_adjustment_)));
    view_holder_node_.SetRotation(display_rotation.x, display_rotation.y,
                                  display_rotation.z, display_rotation.w);
  }

  const DisplayModel::DisplayInfo& display_info =
      display_model_actual_.display_info();

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
    FXL_VLOG(2) << "DisplayModel translation: " << left_offset << ", "
                << top_offset;
  }

  // Today, a layer needs the display's physical dimensions to render correctly.
  layer_.SetSize(static_cast<float>(display_info.width_in_px),
                 static_cast<float>(display_info.height_in_px));

  return true;
}

void Presentation::OnDeviceAdded(mozart::InputDeviceImpl* input_device) {
  FXL_VLOG(1) << "OnDeviceAdded: device_id=" << input_device->id();

  FXL_DCHECK(device_states_by_id_.count(input_device->id()) == 0);

  std::unique_ptr<mozart::DeviceState> state;
  if (input_device->descriptor()->sensor) {
    mozart::OnSensorEventCallback callback =
        [this](uint32_t device_id, fuchsia::ui::input::InputReport event) {
          OnSensorEvent(device_id, std::move(event));
        };
    state = std::make_unique<mozart::DeviceState>(
        input_device->id(), input_device->descriptor(), std::move(callback));
  } else if (input_device->descriptor()->media_buttons) {
    mozart::OnMediaButtonsEventCallback callback =
        [this](fuchsia::ui::input::InputReport report) {
          OnMediaButtonsEvent(std::move(report));
        };
    state = std::make_unique<mozart::DeviceState>(
        input_device->id(), input_device->descriptor(), std::move(callback));
  } else {
    mozart::OnEventCallback callback =
        [this](fuchsia::ui::input::InputEvent event) {
          OnEvent(std::move(event));
        };
    state = std::make_unique<mozart::DeviceState>(
        input_device->id(), input_device->descriptor(), std::move(callback));
  }

  mozart::DeviceState* state_ptr = state.get();
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

void Presentation::OnReport(uint32_t device_id,
                            fuchsia::ui::input::InputReport input_report) {
  TRACE_DURATION("input", "presentation_on_report", "id",
                 input_report.trace_id);
  TRACE_FLOW_END("input", "report_to_presentation", input_report.trace_id);

  FXL_VLOG(2) << "OnReport device=" << device_id
              << ", count=" << device_states_by_id_.count(device_id)
              << ", report=" << input_report;

  if (device_states_by_id_.count(device_id) == 0) {
    FXL_VLOG(1) << "OnReport: Unknown device " << device_id;
    return;
  }

  if (!display_model_initialized_)
    return;

  mozart::DeviceState* state = device_states_by_id_[device_id].second.get();
  fuchsia::math::Size size;
  size.width = display_model_actual_.display_info().width_in_px;
  size.height = display_model_actual_.display_info().height_in_px;

  TRACE_FLOW_BEGIN("input", "report_to_device_state", input_report.trace_id);
  state->Update(std::move(input_report), size);
}

void Presentation::CaptureKeyboardEventHACK(
    fuchsia::ui::input::KeyboardEvent event_to_capture,
    fidl::InterfaceHandle<fuchsia::ui::policy::KeyboardCaptureListenerHACK>
        listener_handle) {
  fuchsia::ui::policy::KeyboardCaptureListenerHACKPtr listener;
  listener.Bind(std::move(listener_handle));
  // Auto-remove listeners if the interface closes.
  listener.set_error_handler(
      [this, listener = listener.get()](zx_status_t status) {
        captured_keybindings_.erase(
            std::remove_if(captured_keybindings_.begin(),
                           captured_keybindings_.end(),
                           [listener](const KeyboardCaptureItem& item) -> bool {
                             return item.listener.get() == listener;
                           }),
            captured_keybindings_.end());
      });

  captured_keybindings_.push_back(
      KeyboardCaptureItem{std::move(event_to_capture), std::move(listener)});
}

void Presentation::CapturePointerEventsHACK(
    fidl::InterfaceHandle<fuchsia::ui::policy::PointerCaptureListenerHACK>
        listener_handle) {
  fuchsia::ui::policy::PointerCaptureListenerHACKPtr listener;
  listener.Bind(std::move(listener_handle));
  // Auto-remove listeners if the interface closes.
  listener.set_error_handler(
      [this, listener = listener.get()](zx_status_t status) {
        captured_pointerbindings_.erase(
            std::remove_if(captured_pointerbindings_.begin(),
                           captured_pointerbindings_.end(),
                           [listener](const PointerCaptureItem& item) -> bool {
                             return item.listener.get() == listener;
                           }),
            captured_pointerbindings_.end());
      });

  captured_pointerbindings_.push_back(PointerCaptureItem{std::move(listener)});
}

void Presentation::GetPresentationMode(GetPresentationModeCallback callback) {
  callback(presentation_mode_);
}

void Presentation::SetPresentationModeListener(
    fidl::InterfaceHandle<fuchsia::ui::policy::PresentationModeListener>
        listener) {
  if (presentation_mode_listener_) {
    FXL_LOG(ERROR) << "Cannot listen to presentation mode; already listening.";
    return;
  }

  if (presentation_mode_detector_ == nullptr) {
    const size_t kDetectorHistoryLength = 5;
    presentation_mode_detector_ =
        std::make_unique<presentation_mode::Detector>(kDetectorHistoryLength);
  }

  presentation_mode_listener_.Bind(std::move(listener));
  FXL_LOG(INFO) << "Presentation mode, now listening.";
}

void Presentation::RegisterMediaButtonsListener(
    fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener>
        listener_handle) {
  MediaButtonsListenerPtr listener;
  listener.Bind(std::move(listener_handle));

  // Auto-remove listeners if the interface closes.
  listener.set_error_handler([this,
                              listener = listener.get()](zx_status_t status) {
    media_buttons_listeners_.erase(
        std::remove_if(media_buttons_listeners_.begin(),
                       media_buttons_listeners_.end(),
                       [listener](const MediaButtonsListenerPtr& item) -> bool {
                         return item.get() == listener;
                       }),
        media_buttons_listeners_.end());
  });

  media_buttons_listeners_.push_back(std::move(listener));
}

bool Presentation::GlobalHooksHandleEvent(
    const fuchsia::ui::input::InputEvent& event) {
  return display_rotater_.OnEvent(event, this) ||
         display_usage_switcher_.OnEvent(event, this) ||
         display_size_switcher_.OnEvent(event, this) ||
         perspective_demo_mode_.OnEvent(event, this) ||
         presentation_switcher_.OnEvent(event, this);
}

void Presentation::OnEvent(fuchsia::ui::input::InputEvent event) {
  TRACE_DURATION("input", "presentation_on_event");
  trace_flow_id_t trace_id = 0;

  FXL_VLOG(1) << "OnEvent " << event;

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

      if (pointer.type == fuchsia::ui::input::PointerEventType::MOUSE) {
        if (cursors_.count(pointer.device_id) == 0) {
          cursors_.emplace(pointer.device_id, CursorState{});
        }

        cursors_[pointer.device_id].position.x = pointer.x;
        cursors_[pointer.device_id].position.y = pointer.y;

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

      glm::vec2 rotated_point =
          display_rotater_.RotatePointerCoordinates(this, pointer.x, pointer.y);
      for (size_t i = 0; i < captured_pointerbindings_.size(); i++) {
        fuchsia::ui::input::PointerEvent clone;
        fidl::Clone(pointer, &clone);

        clone.x = rotated_point.x;
        clone.y = rotated_point.y;

        // Adjust pointer origin with simulated screen offset.
        clone.x -= (display_model_actual_.display_info().width_in_px -
                    display_metrics_.width_in_px()) /
                   2;
        clone.y -= (display_model_actual_.display_info().height_in_px -
                    display_metrics_.height_in_px()) /
                   2;

        // Scale by device pixel density.
        clone.x *= display_metrics_.x_scale_in_pp_per_px();
        clone.y *= display_metrics_.y_scale_in_pp_per_px();

        captured_pointerbindings_[i].listener->OnPointerEvent(std::move(clone));
      }

      fuchsia::ui::input::SendPointerInputCmd pointer_cmd;
      pointer_cmd.pointer_event = std::move(pointer);
      pointer_cmd.compositor_id = compositor_id_;
      input_cmd.set_send_pointer_input(std::move(pointer_cmd));

    } else if (event.is_keyboard()) {
      const fuchsia::ui::input::KeyboardEvent& kbd = event.keyboard();

      for (size_t i = 0; i < captured_keybindings_.size(); i++) {
        const auto& event = captured_keybindings_[i].event;
        if (event.modifiers == kbd.modifiers && event.phase == kbd.phase) {
          if ((event.code_point > 0 && event.code_point == kbd.code_point) ||
              // match on hid_usage when there's no codepoint:
              event.hid_usage == kbd.hid_usage) {
            fuchsia::ui::input::KeyboardEvent clone;
            fidl::Clone(kbd, &clone);
            captured_keybindings_[i].listener->OnEvent(std::move(clone));
            dispatch_event = false;
          }
        }
      }

      fuchsia::ui::input::SendKeyboardInputCmd keyboard_cmd;
      keyboard_cmd.keyboard_event = std::move(kbd);
      keyboard_cmd.compositor_id = compositor_id_;
      input_cmd.set_send_keyboard_input(std::move(keyboard_cmd));
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

void Presentation::OnSensorEvent(uint32_t device_id,
                                 fuchsia::ui::input::InputReport event) {
  FXL_VLOG(2) << "OnSensorEvent(device_id=" << device_id << "): " << event;

  FXL_DCHECK(device_states_by_id_.count(device_id) > 0);
  FXL_DCHECK(device_states_by_id_[device_id].first);
  FXL_DCHECK(device_states_by_id_[device_id].first->descriptor());
  FXL_DCHECK(device_states_by_id_[device_id].first->descriptor()->sensor.get());

  if (presentation_mode_listener_) {
    const fuchsia::ui::input::SensorDescriptor* sensor_descriptor =
        device_states_by_id_[device_id].first->descriptor()->sensor.get();
    std::pair<bool, fuchsia::ui::policy::PresentationMode> update =
        presentation_mode_detector_->Update(*sensor_descriptor,
                                            std::move(event));
    if (update.first && update.second != presentation_mode_) {
      presentation_mode_ = update.second;
      presentation_mode_listener_->OnModeChanged();
    }
  }
}

void Presentation::OnMediaButtonsEvent(fuchsia::ui::input::InputReport report) {
  FXL_CHECK(report.media_buttons);
  fuchsia::ui::input::MediaButtonsEvent event;
  event.set_volume(report.media_buttons->volume);
  event.set_mic_mute(report.media_buttons->mic_mute);

  for (auto& listener : media_buttons_listeners_) {
    listener->OnMediaButtonsEvent(std::move(event));
  }
}

void Presentation::EnableClipping(bool enabled) {
  if (presentation_clipping_enabled_ != enabled) {
    FXL_LOG(INFO) << "enable clipping: " << (enabled ? "true" : "false");
    presentation_clipping_enabled_ = enabled;
    PresentScene();
  }
}

void Presentation::UseOrthographicView() {
  FXL_LOG(INFO) << "Presentation Controller method called: "
                   "UseOrthographicView!! (not implemented)";
}

void Presentation::UsePerspectiveView() {
  FXL_LOG(INFO) << "Presentation Controller method called: "
                   "UsePerspectiveView!! (not implemented)";
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

  bool use_clipping =
      presentation_clipping_enabled_ && perspective_demo_mode_.WantsClipping();
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
          state.position.x * display_metrics_.x_scale_in_pp_per_px() +
              kCursorWidth * .5f,
          state.position.y * display_metrics_.y_scale_in_pp_per_px() +
              kCursorHeight * .5f,
          -kCursorElevation);
    } else if (state.created) {
      state.node->Detach();
      state.created = false;
    }
  }

  session_->Present(0, [weak = weak_factory_.GetWeakPtr()](
                           fuchsia::images::PresentationInfo info) {
    if (auto self = weak.get()) {
      uint64_t next_presentation_time =
          info.presentation_time + info.presentation_interval;

      bool scene_dirty =
          self->session_present_state_ == kPresentPendingAndSceneDirty;

      // Clear the present state.
      self->session_present_state_ = kNoPresentPending;

      scene_dirty |= self->perspective_demo_mode_.UpdateAnimation(
          self, next_presentation_time);
      scene_dirty |=
          self->display_rotater_.UpdateAnimation(self, next_presentation_time);
      if (scene_dirty) {
        self->PresentScene();
      }
    }
  });
}

void Presentation::UpdateLightsForShadowTechnique(
    fuchsia::ui::gfx::ShadowTechnique tech) {
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

void Presentation::SetRendererParams(
    ::std::vector<fuchsia::ui::gfx::RendererParam> params) {
  for (size_t i = 0; i < params.size(); ++i) {
    SetRendererParam(std::move(params[i]));
  }
  session_->Present(0, [](fuchsia::images::PresentationInfo info) {});
}

void Presentation::SetRendererParam(fuchsia::ui::gfx::RendererParam param) {
  switch (param.Which()) {
    case ::fuchsia::ui::gfx::RendererParam::Tag::kShadowTechnique:
      if (renderer_params_override_.shadow_technique.has_value()) {
        FXL_LOG(WARNING)
            << "Presentation::SetRendererParams: Cannot change "
               "shadow technique, default was overriden in root_presenter";
        return;
      }
      UpdateLightsForShadowTechnique(param.shadow_technique());
      break;
    case fuchsia::ui::gfx::RendererParam::Tag::kRenderFrequency:
      if (renderer_params_override_.render_frequency.has_value()) {
        FXL_LOG(WARNING)
            << "Presentation::SetRendererParams: Cannot change "
               "render frequency, default was overriden in root_presenter";
        return;
      }
      break;
    case fuchsia::ui::gfx::RendererParam::Tag::kEnableDebugging:
      if (renderer_params_override_.debug_enabled.has_value()) {
        FXL_LOG(WARNING)
            << "Presentation::SetRendererParams: Cannot change "
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
