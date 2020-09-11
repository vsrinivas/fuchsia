// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/presentation.h"

#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

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

}  // namespace

Presentation::Presentation(
    sys::ComponentContext* component_context, fuchsia::ui::scenic::Scenic* scenic,
    scenic::Session* session, scenic::ResourceId compositor_id,
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request,
    SafePresenter* safe_presenter, ActivityNotifier* activity_notifier,
    int32_t display_startup_rotation_adjustment, std::function<void()> on_client_death)
    : scenic_(scenic),
      session_(session),
      compositor_id_(compositor_id),
      activity_notifier_(activity_notifier),
      layer_(session_),
      renderer_(session_),
      scene_(session_),
      camera_(scene_),
      a11y_session_(scenic),
      display_startup_rotation_adjustment_(display_startup_rotation_adjustment),
      presentation_binding_(this),
      a11y_binding_(this),
      safe_presenter_(safe_presenter),
      safe_presenter_a11y_(&a11y_session_),
      weak_factory_(this) {
  FX_DCHECK(component_context);
  FX_DCHECK(compositor_id != 0);
  FX_DCHECK(safe_presenter_);
  renderer_.SetCamera(camera_);
  layer_.SetRenderer(renderer_);

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
  {
    fuchsia::ui::views::ViewRef root_view_ref, a11y_view_ref;
    {    // Set up views and view holders.
      {  // Set up the root view.
        auto [internal_view_token, internal_view_holder_token] = scenic::ViewTokenPair::New();
        auto [control_ref, view_ref] = scenic::ViewRefPair::New();
        fidl::Clone(view_ref, &root_view_ref);
        root_view_holder_.emplace(session_, std::move(internal_view_holder_token),
                                  "Root View Holder");
        root_view_.emplace(session_, std::move(internal_view_token), std::move(control_ref),
                           std::move(view_ref), "Root View");
      }

      {  // Set up the "accessibility view"
        auto [internal_view_token, internal_view_holder_token] = scenic::ViewTokenPair::New();
        auto [control_ref, view_ref] = scenic::ViewRefPair::New();
        fidl::Clone(view_ref, &a11y_view_ref);
        a11y_view_holder_.emplace(session_, std::move(internal_view_holder_token),
                                  "A11y View Holder");
        a11y_view_.emplace(&a11y_session_, std::move(internal_view_token), std::move(control_ref),
                           std::move(view_ref), "A11y View");
      }

      client_view_holder_.emplace(&a11y_session_, std::move(view_holder_token),
                                  "Client View Holder");

      // Connect it all up.
      scene_.AddChild(root_view_holder_.value());
      root_view_->AddChild(a11y_view_holder_.value());
      a11y_view_->AddChild(client_view_holder_.value());
    }

    injector_.emplace(component_context, /*context*/ std::move(root_view_ref),
                      /*target*/ std::move(a11y_view_ref));
  }

  // Link ourselves to the presentation interface once screen dimensions are
  // available for us to present into.
  FX_CHECK(!presentation_binding_.is_bound());
  presentation_binding_.Bind(std::move(presentation_request));
  scenic_->GetDisplayInfo(
      [weak = weak_factory_.GetWeakPtr()](fuchsia::ui::gfx::DisplayInfo display_info) mutable {
        if (weak) {
          // Get display parameters and propagate values appropriately.
          weak->InitializeDisplayModel(std::move(display_info));
          weak->safe_presenter_->QueuePresent([weak] {
            if (weak) {
              // TODO(fxbug.dev/56345): Stop staggering presents.
              weak->safe_presenter_a11y_.QueuePresent([] {});
            }
          });
        }
      });

  FX_DCHECK(root_view_holder_);
  FX_DCHECK(root_view_);
  FX_DCHECK(a11y_view_holder_);
  FX_DCHECK(a11y_view_);
  FX_DCHECK(client_view_holder_);
  FX_DCHECK(injector_);

  a11y_session_.set_event_handler(
      [on_client_death = std::move(on_client_death),
       client_id = client_view_holder_->id()](std::vector<fuchsia::ui::scenic::Event> events) {
        for (const auto& event : events) {
          if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
              event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewDisconnected &&
              event.gfx().view_disconnected().view_holder_id == client_id) {
            FX_LOGS(WARNING) << "Client died, destroying presentation.";
            on_client_death();  // This kills the Presentation, so exit immediately to be safe.
            return;
          }
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
    safe_presenter_->QueuePresent([weak = weak_factory_.GetWeakPtr()] {
      if (weak) {
        // TODO(fxbug.dev/56345): Stop staggering presents.
        weak->safe_presenter_a11y_.QueuePresent([] {});
      }
    });
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
    // Set the root view to native resolution and orientation (i.e. no rotation) of the display.
    // This lets us delegate touch coordinate transformations to Scenic.
    const float raw_metrics_width = static_cast<float>(display_metrics_.width_in_px());
    const float raw_metrics_height = static_cast<float>(display_metrics_.height_in_px());
    root_view_holder_->SetViewProperties(0.f, 0.f, -kDefaultRootViewDepth, raw_metrics_width,
                                         raw_metrics_height, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
  }

  const bool is_90_degree_rotation = abs(display_startup_rotation_adjustment_ % 180) == 90;

  {  // Set a11y and client views' resolutions to pips.
    float metrics_width = static_cast<float>(display_metrics_.width_in_pp());
    float metrics_height = static_cast<float>(display_metrics_.height_in_pp());

    // Swap metrics on left/right tilt.
    if (is_90_degree_rotation) {
      std::swap(metrics_width, metrics_height);
    }

    a11y_view_holder_->SetViewProperties(0.f, 0.f, -kDefaultRootViewDepth, metrics_width,
                                         metrics_height, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    client_view_holder_->SetViewProperties(0.f, 0.f, -kDefaultRootViewDepth, metrics_width,
                                           metrics_height, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    FX_VLOGS(2) << "DisplayModel layout: " << metrics_width << ", " << metrics_height;
  }

  // Remaining transformations are only applied to a11y view and automatically propagated down to
  // client view through the scene graph.

  {  // Scale a11y view to full device size.
    float metrics_scale_x = display_metrics_.x_scale_in_px_per_pp();
    float metrics_scale_y = display_metrics_.y_scale_in_px_per_pp();
    // Swap metrics on left/right tilt.
    if (is_90_degree_rotation) {
      std::swap(metrics_scale_x, metrics_scale_y);
    }

    a11y_view_holder_->SetScale(metrics_scale_x, metrics_scale_y, 1.f);
    FX_VLOGS(2) << "DisplayModel pixel scale: " << metrics_scale_x << ", " << metrics_scale_y;
  }

  {  // Rotate a11y view to match desired display orientation.
    const glm::quat display_rotation =
        glm::quat(glm::vec3(0, 0, glm::radians<float>(display_startup_rotation_adjustment_)));
    a11y_view_holder_->SetRotation(display_rotation.x, display_rotation.y, display_rotation.z,
                                   display_rotation.w);
  }

  {  // Adjust a11y view position for rotation.
    const float metrics_w = display_metrics_.width_in_px();
    const float metrics_h = display_metrics_.height_in_px();

    float left_offset = 0;
    float top_offset = 0;
    uint32_t degrees_rotated = abs(display_startup_rotation_adjustment_ % 360);
    switch (degrees_rotated) {
      case 0:
        left_offset = 0;
        top_offset = 0;
        break;
      case 90:
        left_offset = metrics_w;
        top_offset = 0;
        break;
      case 180:
        left_offset = metrics_w;
        top_offset = metrics_h;
        break;
      case 270:
        left_offset = 0;
        top_offset = metrics_h;
        break;
      default:
        FX_LOGS(ERROR) << "Unsupported rotation";
        break;
    }

    a11y_view_holder_->SetTranslation(left_offset, top_offset, 0.f);
    FX_VLOGS(2) << "DisplayModel translation: " << left_offset << ", " << top_offset;
  }

  // Today, a layer needs the display's physical dimensions to render correctly.
  layer_.SetSize(static_cast<float>(display_metrics_.width_in_px()),
                 static_cast<float>(display_metrics_.height_in_px()));

  UpdateViewport();

  return true;
}

void Presentation::UpdateViewport() {
  // Viewport should match the visible part of the display 1:1. To do this we need to match the
  // ClipSpaceTransform.
  //
  // Since the ClipSpaceTransform is defined in Vulkan NDC with scaling, and the Viewport is defined
  // in pixel coordinates, we need to be able to transform offsets to pixel coordinates. This is
  // done by multiplying by half the display length and inverting the scale.
  //
  // Because the ClipSpaceTransform is defined with its origin in the center, and the
  // Viewport with its origin in the top left corner, we need to add a center offset to compensate.
  // This turns out to be as simple as half the scaled display length minus half the ClipSpace
  // length, which equals scale - 1 in NDC.
  //
  // Finally, because the ClipSpaceTransform and the Viewport transform are defined in opposite
  // directions (camera to scene vs context to viewport), all the transforms should be inverted
  // for the Viewport transform. This means an inverted scale and negative clip offsets.
  //
  const float display_width = static_cast<float>(display_metrics_.width_in_px());
  const float display_height = static_cast<float>(display_metrics_.height_in_px());
  const float inverted_scale = 1.f / clip_scale_;
  const float ndc_to_pixel_x = inverted_scale * display_width * 0.5f;
  const float ndc_to_pixel_y = inverted_scale * display_height * 0.5f;
  const float center_offset_ndc = clip_scale_ - 1.f;

  injector_->SetViewport({
      .width = display_width,
      .height = display_height,
      .scale = inverted_scale,
      .x_offset = ndc_to_pixel_x * (center_offset_ndc - clip_offset_x_),
      .y_offset = ndc_to_pixel_y * (center_offset_ndc - clip_offset_y_),
  });
}

void Presentation::OnDeviceAdded(ui_input::InputDeviceImpl* input_device) {
  const uint32_t device_id = input_device->id();

  FX_VLOGS(1) << "OnDeviceAdded: device_id=" << device_id;

  FX_DCHECK(device_states_by_id_.count(device_id) == 0);
  std::unique_ptr<ui_input::DeviceState> state;

  if (input_device->descriptor()->sensor) {
    ui_input::OnSensorEventCallback callback = [this](uint32_t device_id,
                                                      fuchsia::ui::input::InputReport event) {
      OnSensorEvent(device_id, std::move(event));
    };
    state = std::make_unique<ui_input::DeviceState>(device_id, input_device->descriptor(),
                                                    std::move(callback));
  } else {
    ui_input::OnEventCallback callback = [this](fuchsia::ui::input::InputEvent event) {
      OnEvent(std::move(event));
    };
    state = std::make_unique<ui_input::DeviceState>(device_id, input_device->descriptor(),
                                                    std::move(callback));
  }

  ui_input::DeviceState* state_ptr = state.get();
  auto device_pair = std::make_pair(input_device, std::move(state));
  state_ptr->OnRegistered();
  device_states_by_id_.emplace(device_id, std::move(device_pair));

  injector_->OnDeviceAdded(device_id);
}

void Presentation::OnDeviceRemoved(uint32_t device_id) {
  FX_VLOGS(1) << "OnDeviceRemoved: device_id=" << device_id;

  if (device_states_by_id_.count(device_id) != 0) {
    device_states_by_id_[device_id].second->OnUnregistered();
    device_states_by_id_.erase(device_id);
  }

  injector_->OnDeviceRemoved(device_id);
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
  clip_offset_x_ = x;
  clip_offset_y_ = y;
  clip_scale_ = scale;
  camera_.SetClipSpaceTransform(clip_offset_x_, clip_offset_y_, clip_scale_);
  // The callback is used to throttle magnification transition animations and is expected to
  // approximate the framerate.
  safe_presenter_->QueuePresent([this, callback = std::move(callback)] {
    callback();
    UpdateViewport();
  });
}

void Presentation::ResetClipSpaceTransform() {
  SetClipSpaceTransform(0, 0, 1, [] {});
}

void Presentation::OnEvent(fuchsia::ui::input::InputEvent event) {
  TRACE_DURATION("input", "presentation_on_event");
  FX_VLOGS(1) << "OnEvent " << event;

  activity_notifier_->ReceiveInputEvent(event);

  injector_->OnEvent(event);
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
