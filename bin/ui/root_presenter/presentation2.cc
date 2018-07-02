// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/presentation2.h"

#include <cmath>
#include <utility>

#if defined(countof)
// Workaround for compiler error due to Zircon defining countof() as a macro.
// Redefines countof() using GLM_COUNTOF(), which currently provides a more
// sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif
#include <glm/ext.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "lib/app/cpp/connect.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/views/cpp/formatting.h"

#include "garnet/bin/ui/root_presenter/displays/display_configuration.h"

namespace root_presenter {
namespace {

// The shape and elevation of the cursor.
constexpr float kCursorWidth = 20;
constexpr float kCursorHeight = 20;
constexpr float kCursorRadius = 10;
constexpr float kCursorElevation = 800;

}  // namespace

Presentation2::Presentation2(fuchsia::ui::scenic::Scenic* scenic,
                             scenic::Session* session,
                             zx::eventpair view_holder_token,
                             RendererParams renderer_params)
    : scenic_(scenic),
      session_(session),
      layer_(session_),
      renderer_(session_),
      scene_(session_),
      camera_(scene_),
      ambient_light_(session_),
      directional_light_(session_),
      view_holder_node_(session),
      root_node_(session_),
      view_holder_(session, std::move(view_holder_token), "root_presenter"),
      cursor_shape_(session_, kCursorWidth, kCursorHeight, 0u, kCursorRadius,
                    kCursorRadius, kCursorRadius),
      cursor_material_(session_),
      presentation_binding_(this),
      renderer_params_override_(renderer_params),
      weak_factory_(this) {
  renderer_.SetCamera(camera_);
  layer_.SetRenderer(renderer_);
  scene_.AddChild(root_node_);
  root_node_.AddChild(view_holder_node_);
  view_holder_node_.Attach(view_holder_);

  scene_.AddLight(ambient_light_);
  scene_.AddLight(directional_light_);
  ambient_light_.SetColor(0.3f, 0.3f, 0.3f);
  directional_light_.SetColor(0.7f, 0.7f, 0.7f);
  light_direction_ = glm::vec3(1.f, 1.f, -2.f);
  directional_light_.SetDirection(light_direction_.x, light_direction_.y,
                                  light_direction_.z);

  cursor_material_.SetColor(0xff, 0x00, 0xff, 0xff);

  session_->set_event_handler(
      fit::bind_member(this, &Presentation2::HandleScenicEvents));

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
  }
}

Presentation2::~Presentation2() {}

void Presentation2::PresentView(
    ::fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
        presentation_request,
    YieldCallback yield_callback, ShutdownCallback shutdown_callback) {
  FXL_DCHECK(!display_model_initialized_);

  yield_callback_ = std::move(yield_callback);
  shutdown_callback_ = std::move(shutdown_callback);

  scenic_->GetDisplayInfo(fxl::MakeCopyable(
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
      }));
}

void Presentation2::InitializeDisplayModel(
    fuchsia::ui::gfx::DisplayInfo display_info) {
  FXL_DCHECK(!display_model_initialized_);

  // Save previous display values. These could have been overriden by earlier
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

void Presentation2::HandleScenicEvent(const fuchsia::ui::scenic::Event& event) {
  switch (event.Which()) {
    case fuchsia::ui::scenic::Event::Tag::kGfx:
      switch (event.gfx().Which()) {
        case fuchsia::ui::gfx::Event::Tag::kViewDisconnected: {
          auto& evt = event.gfx().view_disconnected();
          FXL_DCHECK(view_holder_.id() == evt.view_holder_id);
          FXL_LOG(ERROR)
              << "Root presenter: Content view terminated unexpectedly.";
          Shutdown();
          break;
        }
        default: { break; }
      }
    default: { break; }
  }
}

void Presentation2::HandleScenicEvents(
    fidl::VectorPtr<fuchsia::ui::scenic::Event> events) {
  for (auto& event : *events) {
    HandleScenicEvent(event);
  }
}

void Presentation2::SetDisplaySizeInMm(float width_in_mm, float height_in_mm) {
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

  FXL_LOG(INFO) << "Presentation2::SetDisplaySizeInMm: changing display "
                   "dimensions to "
                << "width="
                << display_model_simulated_.display_info().width_in_mm << "mm, "
                << "height="
                << display_model_simulated_.display_info().height_in_mm
                << "mm.";

  ApplyDisplayModelChanges(true, true);
}

void Presentation2::SetDisplayRotation(float display_rotation_degrees,
                                       bool animate) {
  display_rotater_.SetDisplayRotation(this, display_rotation_degrees, animate);
};

bool Presentation2::SetDisplaySizeInMmWithoutApplyingChanges(
    float width_in_mm, float height_in_mm, bool print_errors) {
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
      FXL_LOG(ERROR) << "Presentation2::SetDisplaySizeInMm: tried to change "
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
      FXL_LOG(ERROR) << "Presentation2::SetDisplaySizeInMm: tried to change "
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

void Presentation2::SetDisplayUsage(fuchsia::ui::policy::DisplayUsage usage) {
  fuchsia::ui::policy::DisplayUsage old_usage =
      display_model_simulated_.environment_info().usage;
  SetDisplayUsageWithoutApplyingChanges(usage);
  if (display_model_simulated_.environment_info().usage == old_usage) {
    // Nothing needs to be changed.
    return;
  }

  ApplyDisplayModelChanges(true, true);

  FXL_LOG(INFO) << "Presentation2::SetDisplayUsage: changing display usage to "
                << GetDisplayUsageAsString(
                       display_model_simulated_.environment_info().usage);
}

void Presentation2::SetDisplayUsageWithoutApplyingChanges(
    fuchsia::ui::policy::DisplayUsage usage) {
  display_model_simulated_.environment_info().usage =
      (usage == fuchsia::ui::policy::DisplayUsage::kUnknown)
          ? display_model_actual_.environment_info().usage
          : usage;
}

bool Presentation2::ApplyDisplayModelChanges(bool print_log,
                                             bool present_changes) {
  bool updated = ApplyDisplayModelChangesHelper(print_log);

  if (updated && present_changes) {
    PresentScene();
  }
  return updated;
}

bool Presentation2::ApplyDisplayModelChangesHelper(bool print_log) {
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

  view_holder_.SetViewProperties(0.f, 0.f, 0.f, display_metrics_.width_in_pp(),
                                 display_metrics_.height_in_pp(), 1000.f, 0.f,
                                 0.f, 0.f, 0.f, 0.f, 0.f);

  // Apply device pixel ratio.
  scene_.SetScale(display_metrics_.x_scale_in_px_per_pp(),
                  display_metrics_.y_scale_in_px_per_pp(), 1.f);

  // Apply rotation.
  float anchor_x = display_metrics_.width_in_pp() / 2;
  float anchor_y = display_metrics_.height_in_pp() / 2;

  view_holder_node_.SetAnchor(anchor_x, anchor_y, 0);

  glm::quat display_rotation = glm::quat(
      glm::vec3(0, 0, glm::radians<float>(display_rotation_current_)));
  view_holder_node_.SetRotation(display_rotation.x, display_rotation.y,
                                display_rotation.z, display_rotation.w);

  // Center everything.
  float left_offset = (display_model_actual_.display_info().width_in_px -
                       display_metrics_.width_in_px()) /
                      2;
  float top_offset = (display_model_actual_.display_info().height_in_px -
                      display_metrics_.height_in_px()) /
                     2;
  view_holder_node_.SetTranslation(
      left_offset / display_metrics_.x_scale_in_px_per_pp(),
      top_offset / display_metrics_.y_scale_in_px_per_pp(), 0.f);

  layer_.SetSize(
      static_cast<float>(display_model_actual_.display_info().width_in_px),
      static_cast<float>(display_model_actual_.display_info().height_in_px));
  return true;
}

void Presentation2::OnDeviceAdded(mozart::InputDeviceImpl* input_device) {
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

void Presentation2::OnDeviceRemoved(uint32_t device_id) {
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

void Presentation2::OnReport(uint32_t device_id,
                             fuchsia::ui::input::InputReport input_report) {
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
  state->Update(std::move(input_report), size);
}

void Presentation2::CaptureKeyboardEventHACK(
    fuchsia::ui::input::KeyboardEvent event_to_capture,
    fidl::InterfaceHandle<fuchsia::ui::policy::KeyboardCaptureListenerHACK>
        listener_handle) {
  fuchsia::ui::policy::KeyboardCaptureListenerHACKPtr listener;
  listener.Bind(std::move(listener_handle));
  // Auto-remove listeners if the interface closes.
  listener.set_error_handler([this, listener = listener.get()] {
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

void Presentation2::CapturePointerEventsHACK(
    fidl::InterfaceHandle<fuchsia::ui::policy::PointerCaptureListenerHACK>
        listener_handle) {
  fuchsia::ui::policy::PointerCaptureListenerHACKPtr listener;
  listener.Bind(std::move(listener_handle));
  // Auto-remove listeners if the interface closes.
  listener.set_error_handler([this, listener = listener.get()] {
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

void Presentation2::GetPresentationMode(GetPresentationModeCallback callback) {
  callback(presentation_mode_);
}

void Presentation2::SetPresentationModeListener(
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

bool Presentation2::GlobalHooksHandleEvent(
    const fuchsia::ui::input::InputEvent& event) {
  return display_rotater_.OnEvent(event, this) ||
         display_usage_switcher_.OnEvent(event, this) ||
         display_size_switcher_.OnEvent(event, this) ||
         perspective_demo_mode_.OnEvent(event, this) ||
         presentation_switcher_.OnEvent(event, this);
}

void Presentation2::OnEvent(fuchsia::ui::input::InputEvent event) {
  FXL_VLOG(1) << "OnEvent " << event;

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

      for (size_t i = 0; i < captured_pointerbindings_.size(); i++) {
        fuchsia::ui::input::PointerEvent clone;
        fidl::Clone(pointer, &clone);

        glm::vec2 rotated_point =
            display_rotater_.RotatePointerCoordinates(this, clone.x, clone.y);
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

    } else if (event.is_keyboard()) {
      const fuchsia::ui::input::KeyboardEvent& kbd = event.keyboard();

      for (size_t i = 0; i < captured_keybindings_.size(); i++) {
        const auto& event = captured_keybindings_[i].event;
        if ((kbd.modifiers & event.modifiers) && (event.phase == kbd.phase) &&
            (event.code_point == kbd.code_point)) {
          fuchsia::ui::input::KeyboardEvent clone;
          fidl::Clone(kbd, &clone);
          captured_keybindings_[i].listener->OnEvent(std::move(clone));
        }
      }
    }
  }

  if (invalidate) {
    PresentScene();
  }

  if (dispatch_event && input_dispatcher_)
    input_dispatcher_->DispatchEvent(std::move(event));
}

void Presentation2::OnSensorEvent(uint32_t device_id,
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

// |Presentation|
void Presentation2::EnableClipping(bool enabled) {
  if (presentation_clipping_enabled_ != enabled) {
    FXL_LOG(INFO) << "enable clipping: " << (enabled ? "true" : "false");
    presentation_clipping_enabled_ = enabled;
    PresentScene();
  }
}

// |Presentation|
void Presentation2::UseOrthographicView() {
  FXL_LOG(INFO) << "Presentation Controller method called: "
                   "UseOrthographicView!! (not implemented)";
}

// |Presentation|
void Presentation2::UsePerspectiveView() {
  FXL_LOG(INFO) << "Presentation Controller method called: "
                   "UsePerspectiveView!! (not implemented)";
}

void Presentation2::PresentScene() {
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
          kCursorElevation);
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

void Presentation2::Shutdown() { shutdown_callback_(); }

void Presentation2::SetRendererParams(
    ::fidl::VectorPtr<fuchsia::ui::gfx::RendererParam> params) {
  for (size_t i = 0; i < params->size(); ++i) {
    switch (params->at(i).Which()) {
      case ::fuchsia::ui::gfx::RendererParam::Tag::kShadowTechnique:
        if (renderer_params_override_.shadow_technique.has_value()) {
          FXL_LOG(WARNING)
              << "Presentation2::SetRendererParams: Cannot change "
                 "shadow technique, default was overriden in root_presenter";
          continue;
        }
        break;
      case fuchsia::ui::gfx::RendererParam::Tag::kRenderFrequency:
        if (renderer_params_override_.render_frequency.has_value()) {
          FXL_LOG(WARNING)
              << "Presentation2::SetRendererParams: Cannot change "
                 "render frequency, default was overriden in root_presenter";
          continue;
        }
        break;
      case fuchsia::ui::gfx::RendererParam::Tag::Invalid:
        continue;
    }
    renderer_.SetParam(std::move(params->at(i)));
  }
  session_->Present(0, [](fuchsia::images::PresentationInfo info) {});
}

}  // namespace root_presenter
