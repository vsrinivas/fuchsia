// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/presentation.h"

#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/ui/bin/root_presenter/safe_presenter.h"

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on
#include <glm/ext.hpp>

#include <cmath>

#include "src/ui/bin/root_presenter/displays/display_configuration.h"

namespace root_presenter {
namespace {

// TODO(SCN-1276): Don't hardcode Z bounds in multiple locations.
constexpr float kDefaultRootViewDepth = 1000;

// TODO(SCN-1278): Remove this.
// Turn two floats (high bits, low bits) into a 64-bit uint.
trace_flow_id_t PointerTraceHACK(float fa, float fb) {
  uint32_t ia, ib;
  memcpy(&ia, &fa, sizeof(uint32_t));
  memcpy(&ib, &fb, sizeof(uint32_t));
  return (((uint64_t)ia) << 32) | ib;
}

}  // namespace

Presentation::Presentation(
    fuchsia::ui::scenic::Scenic* scenic, scenic::Session* session, scenic::ResourceId compositor_id,
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request,
    SafePresenter* safe_presenter, ActivityNotifier* activity_notifier,
    int32_t display_startup_rotation_adjustment)
    : scenic_(scenic),
      session_(session),
      compositor_id_(compositor_id),
      activity_notifier_(activity_notifier),
      layer_(session_),
      renderer_(session_),
      scene_(session_),
      camera_(scene_),
      view_holder_node_(session),
      root_node_(session_),
      view_holder_(session, std::move(view_holder_token), "root_presenter"),
      display_startup_rotation_adjustment_(display_startup_rotation_adjustment),
      presentation_binding_(this),
      a11y_binding_(this),
      safe_presenter_(safe_presenter),
      weak_factory_(this) {
  FX_DCHECK(compositor_id != 0);
  FX_DCHECK(safe_presenter_);
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
  scenic::AmbientLight ambient_light(session_);
  scenic::DirectionalLight directional_light(session_);
  scenic::PointLight point_light(session_);
  scene_.AddLight(ambient_light);
  scene_.AddLight(directional_light);
  scene_.AddLight(point_light);
  directional_light.SetDirection(1.f, 1.f, 2.f);
  point_light.SetPosition(300.f, 300.f, -2000.f);
  point_light.SetFalloff(0.f);

  // Explicitly set "UNSHADOWED" as the default shadow type. In addition to
  // setting the param, this sets appropriate light intensities.
  {
    // When no shadows, ambient light needs to be full brightness.  Otherwise,
    // ambient needs to be dimmed so that other lights don't "overbrighten".
    ambient_light.SetColor(1.f, 1.f, 1.f);
    directional_light.SetColor(0.f, 0.f, 0.f);
    point_light.SetColor(0.f, 0.f, 0.f);
    fuchsia::ui::gfx::RendererParam param;
    param.set_shadow_technique(fuchsia::ui::gfx::ShadowTechnique::UNSHADOWED);
    renderer_.SetParam(std::move(param));
  }

  SetScenicDisplayRotation();

  // Link ourselves to the presentation interface once screen dimensions are
  // available for us to present into.
  FX_CHECK(!presentation_binding_.is_bound());
  presentation_binding_.Bind(std::move(presentation_request));
  scenic_->GetDisplayInfo(
      [weak = weak_factory_.GetWeakPtr()](fuchsia::ui::gfx::DisplayInfo display_info) mutable {
        if (weak) {
          // Get display parameters and propagate values appropriately.
          weak->InitializeDisplayModel(std::move(display_info));
          weak->safe_presenter_->QueuePresent([] {});
        }
      });
}

Presentation::~Presentation() = default;

void Presentation::RegisterWithMagnifier(fuchsia::accessibility::Magnifier* magnifier) {
  magnifier->RegisterHandler(a11y_binding_.NewBinding());
  a11y_binding_.set_error_handler([this](auto) { ResetClipSpaceTransform(); });
}

void Presentation::InitializeDisplayModel(fuchsia::ui::gfx::DisplayInfo display_info) {
  FX_DCHECK(!display_model_initialized_);

  // Initialize display model.
  display_configuration::InitializeModelForDisplay(display_info.width_in_px,
                                                   display_info.height_in_px, &display_model_);

  display_model_initialized_ = true;

  ApplyDisplayModelChanges(true, false);
}

bool Presentation::ApplyDisplayModelChanges(bool print_log, bool present_changes) {
  bool updated = ApplyDisplayModelChangesHelper(print_log);

  if (updated && present_changes) {
    safe_presenter_->QueuePresent([] {});
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
    FX_VLOGS(2) << "DisplayModel layout: " << metrics_width << ", " << metrics_height;
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
    FX_VLOGS(2) << "DisplayModel pixel scale: " << metrics_scale_x << ", " << metrics_scale_y;
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
    FX_VLOGS(2) << "DisplayModel anchor: " << anchor_x << ", " << anchor_y;
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
    FX_VLOGS(2) << "DisplayModel translation: " << left_offset << ", " << top_offset;
  }

  // Today, a layer needs the display's physical dimensions to render correctly.
  layer_.SetSize(static_cast<float>(display_info.width_in_px),
                 static_cast<float>(display_info.height_in_px));

  return true;
}

void Presentation::OnDeviceAdded(ui_input::InputDeviceImpl* input_device) {
  FX_VLOGS(1) << "OnDeviceAdded: device_id=" << input_device->id();

  FX_DCHECK(device_states_by_id_.count(input_device->id()) == 0);

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
  FX_VLOGS(1) << "OnDeviceRemoved: device_id=" << device_id;

  if (device_states_by_id_.count(device_id) != 0) {
    device_states_by_id_[device_id].second->OnUnregistered();
    device_states_by_id_.erase(device_id);
  }
}

void Presentation::OnReport(uint32_t device_id, fuchsia::ui::input::InputReport input_report) {
  // Media buttons should be processed by MediaButtonsHandler.
  FX_DCHECK(!input_report.media_buttons);
  TRACE_DURATION("input", "presentation_on_report", "id", input_report.trace_id);
  TRACE_FLOW_END("input", "report_to_presentation", input_report.trace_id);

  FX_VLOGS(2) << "OnReport device=" << device_id
              << ", count=" << device_states_by_id_.count(device_id) << ", report=" << input_report;

  if (device_states_by_id_.count(device_id) == 0) {
    FX_VLOGS(1) << "OnReport: Unknown device " << device_id;
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

void Presentation::SetClipSpaceTransform(float x, float y, float scale,
                                         SetClipSpaceTransformCallback callback) {
  camera_.SetClipSpaceTransform(x, y, scale);
  // The callback is used to throttle magnification transition animations and is expected to
  // approximate the framerate.
  safe_presenter_->QueuePresent([callback = std::move(callback)] { callback(); });
}

void Presentation::ResetClipSpaceTransform() {
  SetClipSpaceTransform(0, 0, 1, [] {});
}

void Presentation::OnEvent(fuchsia::ui::input::InputEvent event) {
  TRACE_DURATION("input", "presentation_on_event");
  FX_VLOGS(1) << "OnEvent " << event;

  activity_notifier_->ReceiveInputEvent(event);

  if (!event.is_pointer()) {
    FX_LOGS(ERROR) << "Only pointer input events are handled.";
    return;
  }

  // TODO(SCN-1278): Use proper trace_id for tracing flow.
  const trace_flow_id_t trace_id =
      PointerTraceHACK(event.pointer().radius_major, event.pointer().radius_minor);
  TRACE_FLOW_END("input", "dispatch_event_to_presentation", trace_id);

  fuchsia::ui::input::Command input_cmd;
  {
    fuchsia::ui::input::SendPointerInputCmd pointer_cmd;
    pointer_cmd.pointer_event = std::move(event.pointer());
    pointer_cmd.compositor_id = compositor_id_;
    input_cmd.set_send_pointer_input(std::move(pointer_cmd));
  }

  TRACE_FLOW_BEGIN("input", "dispatch_event_to_scenic", trace_id);
  session_->Enqueue(std::move(input_cmd));
}

void Presentation::OnSensorEvent(uint32_t device_id, fuchsia::ui::input::InputReport event) {
  FX_VLOGS(2) << "OnSensorEvent(device_id=" << device_id << "): " << event;

  FX_DCHECK(device_states_by_id_.count(device_id) > 0);
  FX_DCHECK(device_states_by_id_[device_id].first);
  FX_DCHECK(device_states_by_id_[device_id].first->descriptor());
  FX_DCHECK(device_states_by_id_[device_id].first->descriptor()->sensor.get());

  // No clients of sensor events at the moment.
}

void Presentation::SetScenicDisplayRotation() {
  fuchsia::ui::gfx::Command command;
  fuchsia::ui::gfx::SetDisplayRotationCmdHACK display_rotation_cmd;
  display_rotation_cmd.compositor_id = compositor_id_;
  display_rotation_cmd.rotation_degrees = display_startup_rotation_adjustment_;
  command.set_set_display_rotation(std::move(display_rotation_cmd));
  session_->Enqueue(std::move(command));
}

}  // namespace root_presenter
