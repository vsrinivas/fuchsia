// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/perspective_demo_mode.h"

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on

#include <array>
#include <glm/ext.hpp>

#include "src/ui/bin/root_presenter/presentation.h"

namespace root_presenter {

namespace {
constexpr float kPi = glm::pi<float>();
}  // namespace

PerspectiveDemoMode::PerspectiveDemoMode() {}

bool PerspectiveDemoMode::OnEvent(const fuchsia::ui::input::InputEvent& event,
                                  Presentation* presenter) {
  if (event.is_pointer()) {
    const fuchsia::ui::input::PointerEvent& pointer = event.pointer();
    if (animation_state_ == kThreeQuarters || animation_state_ == kPerspective) {
      if (pointer.phase == fuchsia::ui::input::PointerEventPhase::DOWN) {
        // If we're not already panning/rotating the camera, then start, but
        // only if the touch-down is in the bottom 10% of the screen.
        if (!trackball_pointer_down_ &&
            pointer.y > 0.9f * presenter->display_model_.display_info().height_in_px) {
          trackball_pointer_down_ = true;
          trackball_device_id_ = pointer.device_id;
          trackball_pointer_id_ = pointer.pointer_id;
          trackball_previous_x_ = pointer.x;
        }
      } else if (pointer.phase == fuchsia::ui::input::PointerEventPhase::MOVE) {
        // If the moved pointer is the one that is currently panning/rotating
        // the camera, then update the camera position.
        if (trackball_pointer_down_ && trackball_device_id_ == pointer.device_id &&
            trackball_device_id_ == pointer.device_id) {
          float rate = -2.5f / presenter->display_model_.display_info().width_in_px;
          float change = rate * (pointer.x - trackball_previous_x_);
          trackball_previous_x_ = pointer.x;

          if (animation_state_ == kThreeQuarters) {
            target_camera_pan_ += change;
            target_camera_pan_ = glm::clamp(target_camera_pan_, -1.f, 1.f);
          } else if (animation_state_ == kPerspective) {
            target_camera_zoom_ += change;
            target_camera_zoom_ = glm::clamp(target_camera_zoom_, 0.0f, 1.0f);
            float fov = 360.f * ComputeHalfFov(presenter, target_camera_zoom_) / kPi;
            FXL_LOG(INFO) << "Current perspective fov is " << fov << "degrees";
          }
        }
      }
    }

    // Pointer release should be handled no matter which state we are in.
    if (pointer.phase == fuchsia::ui::input::PointerEventPhase::UP) {
      // The pointer was released.
      if (trackball_pointer_down_ && trackball_device_id_ == pointer.device_id &&
          trackball_device_id_ == pointer.device_id) {
        trackball_pointer_down_ = false;
      }
    }
  } else if (event.is_keyboard()) {
    // Alt-Backspace cycles through modes.
    const fuchsia::ui::input::KeyboardEvent& kbd = event.keyboard();
    if ((kbd.modifiers & fuchsia::ui::input::kModifierAlt) &&
        kbd.phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED && kbd.code_point == 0 &&
        kbd.hid_usage == 42 && !trackball_pointer_down_) {
      HandleAltBackspace(presenter);
      return true;
    }
  }

  return false;
}

void PerspectiveDemoMode::HandleAltBackspace(Presentation* presenter) {
  switch (animation_state_) {
    case kOrthographic:
      target_camera_pan_ = 0.0f;
      target_camera_zoom_ = 0.0f;
      animation_state_ = kAnimateToThreeQuarters;
      animation_start_time_ = zx_clock_get_monotonic();
      break;
    case kThreeQuarters:
      animation_state_ = kAnimateToPerspective;
      animation_start_time_ = zx_clock_get_monotonic();
      break;
    case kPerspective:
      animation_state_ = kAnimateToOrthographic;
      animation_start_time_ = zx_clock_get_monotonic();
    default:
      return;
  }

  UpdateAnimation(presenter, animation_start_time_);
}

float PerspectiveDemoMode::ComputeHalfFov(Presentation* presenter, float zoom) const {
  // The default camera emulates an orthographic camera, by creating a .1-degree
  // half angle camera, at the appropriate distance.
  constexpr float kMinHalfFov = .1f * kPi / 180.f;

  // TODO(SCN-194): The maximum half fov is determined by the minimum camera
  // distance. This distance matches the hard coded behavior from
  // escher::Camera::NewOrtho() and scenic::gfx::Layer::GetViewingVolume(). For
  // a 1600px height display, this works out to ~76 degrees.
  float max_half_fov = atan(presenter->display_model_.display_info().height_in_px * 0.5f / 1010.f);

  return glm::lerp(kMinHalfFov, max_half_fov, zoom);
}

void PerspectiveDemoMode::UpdateCamera(Presentation* presenter, float pan_param, float zoom_param) {
  const float half_width = presenter->display_model_.display_info().width_in_px * 0.5f;
  const float half_height = presenter->display_model_.display_info().height_in_px * 0.5f;

  // Always look at the middle of the stage.
  const std::array<float, 3> target = {half_width, half_height, 0};

  // Ease-in/ease-out for the animation.
  pan_param = glm::smoothstep(0.f, 1.f, pan_param);
  zoom_param = glm::smoothstep(0.f, 1.f, zoom_param);

  // The target camera takes into account the current authored pan and zoom
  // requests.
  float zoom = glm::lerp(0.f, target_camera_zoom_, zoom_param);
  float half_fovy = ComputeHalfFov(presenter, zoom);
  float eye_dist = half_height / tan(half_fovy);
  float eye_z = -eye_dist;
  glm::vec3 eye_start(half_width, half_height, eye_z);

  constexpr float kMaxCameraPan = kPi / 4;
  float eye_end_x = sin(glm::lerp(0.f, kMaxCameraPan, target_camera_pan_)) * eye_dist + half_width;
  float eye_end_y = cos(glm::lerp(0.f, kMaxCameraPan, target_camera_pan_)) * eye_dist + half_height;

  glm::vec3 eye_end(eye_end_x, eye_end_y, 0.75f * eye_z);

  // Halfway point for the pan animation is further out than the starting point,
  // to get a cool zoom out->zoom in effect.
  glm::vec3 eye_mid = glm::mix(eye_start, eye_end, 0.4f);
  eye_mid.z = 1.5f * eye_z;

  // Quadratic bezier.
  glm::vec3 eye = glm::mix(glm::mix(eye_start, eye_mid, pan_param),
                           glm::mix(eye_mid, eye_end, pan_param), pan_param);

  glm::vec3 glm_up = glm::mix(glm::vec3(0, -1.f, 0.f), glm::vec3(0, -0.1f, -0.9f), pan_param);
  glm_up = glm::normalize(glm_up);

  const std::array<float, 3> up = {glm_up[0], glm_up[1], glm_up[2]};
  presenter->camera_.SetTransform({eye.x, eye.y, eye.z}, target, up);
  presenter->camera_.SetProjection(2.f * half_fovy);
}

bool PerspectiveDemoMode::UpdateAnimation(Presentation* presenter, uint64_t presentation_time) {
  if (animation_state_ == kOrthographic) {
    return false;
  }

  double secs = static_cast<double>(presentation_time - animation_start_time_) / 1'000'000'000;
  constexpr double kAnimationDuration = 1.3;
  float time_param = secs / kAnimationDuration;

  if (time_param >= 1.f) {
    time_param = 1.f;
    switch (animation_state_) {
      case kAnimateToThreeQuarters:
        animation_state_ = kThreeQuarters;
        break;
      case kAnimateToPerspective:
        animation_state_ = kPerspective;
        break;
      case kAnimateToOrthographic: {
        animation_state_ = kOrthographic;

        const float half_width = presenter->display_model_.display_info().width_in_px * 0.5f;
        const float half_height = presenter->display_model_.display_info().height_in_px * 0.5f;

        // Always look at the middle of the stage.
        const std::array<float, 3> target = {half_width, half_height, 0};

        glm::vec3 glm_up(0, -1.f, 0.f);
        glm_up = glm::normalize(glm_up);
        std::array<float, 3> up = {glm_up[0], glm_up[1], glm_up[2]};

        // Switch back to ortho view, and re-enable clipping.
        // TODO(SCN-1276): Don't hardcode Z bounds in multiple locations.
        std::array<float, 3> ortho_eye = {half_width, half_height, -1010.f};
        presenter->camera_.SetTransform(ortho_eye, target, up);
        presenter->camera_.SetProjection(0.f);
        return true;
      }
      default:
        break;
    }
  }

  float pan_param;
  float zoom_param;
  switch (animation_state_) {
    case kAnimateToThreeQuarters:
      pan_param = time_param;
      zoom_param = 0.f;
      break;
    case kAnimateToPerspective:
      pan_param = 1.f - time_param;
      zoom_param = time_param;
      break;
    case kAnimateToOrthographic:
      pan_param = 0.f;
      zoom_param = 1.f - time_param;
      break;
    case kThreeQuarters:
      pan_param = 1.f;
      zoom_param = 0.f;
      break;
    case kPerspective:
      pan_param = 0.f;
      zoom_param = 1.f;
      break;
    default:
      FXL_DCHECK(false);
      return false;
  }

  UpdateCamera(presenter, pan_param, zoom_param);

  return true;
}

}  // namespace root_presenter
