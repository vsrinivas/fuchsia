// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/perspective_demo_mode.h"

#include <array>

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
//#include <glm/gtc/type_ptr.hpp>

#include "garnet/bin/ui/root_presenter/presentation.h"

namespace root_presenter {

namespace {
constexpr float kPi = glm::pi<float>();
}  // namespace

PerspectiveDemoMode::PerspectiveDemoMode() {}

bool PerspectiveDemoMode::OnEvent(const fuchsia::ui::input::InputEvent& event,
                                  Presentation* presenter) {
  if (event.is_pointer()) {
    const fuchsia::ui::input::PointerEvent& pointer = event.pointer();
    if (animation_state_ == kTrackball) {
      if (pointer.phase == fuchsia::ui::input::PointerEventPhase::DOWN) {
        // If we're not already panning/rotating the camera, then start, but
        // only if the touch-down is in the bottom 10% of the screen.
        if (!trackball_pointer_down_ &&
            pointer.y > 0.9f * presenter->display_metrics().height_in_pp()) {
          trackball_pointer_down_ = true;
          trackball_device_id_ = pointer.device_id;
          trackball_pointer_id_ = pointer.pointer_id;
          trackball_previous_x_ = pointer.x;
        }
      } else if (pointer.phase == fuchsia::ui::input::PointerEventPhase::MOVE) {
        // If the moved pointer is the one that is currently panning/rotating
        // the camera, then update the camera position.
        if (trackball_pointer_down_ &&
            trackball_device_id_ == pointer.device_id &&
            trackball_device_id_ == pointer.device_id) {
          float pan_rate = -2.5f / presenter->display_metrics().width_in_pp();
          float pan_change = pan_rate * (pointer.x - trackball_previous_x_);
          trackball_previous_x_ = pointer.x;

          camera_pan_ += pan_change;
          if (camera_pan_ < -1.f) {
            camera_pan_ = -1.f;
          } else if (camera_pan_ > 1.f) {
            camera_pan_ = 1.f;
          }
        }
      } else if (pointer.phase == fuchsia::ui::input::PointerEventPhase::UP) {
        // The pointer was released.
        if (trackball_pointer_down_ &&
            trackball_device_id_ == pointer.device_id &&
            trackball_device_id_ == pointer.device_id) {
          trackball_pointer_down_ = false;
        }
      }
    }
  } else if (event.is_keyboard()) {
    // Alt-Backspace cycles through modes.
    const fuchsia::ui::input::KeyboardEvent& kbd = event.keyboard();
    if ((kbd.modifiers & fuchsia::ui::input::kModifierAlt) &&
        kbd.phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED &&
        kbd.code_point == 0 && kbd.hid_usage == 42 &&
        !trackball_pointer_down_) {
      HandleAltBackspace(presenter);
      return true;
    }
  }

  return false;
}

void PerspectiveDemoMode::HandleAltBackspace(Presentation* presenter) {
  switch (animation_state_) {
    case kDefault:
      animation_state_ = kCameraMovingAway;
      break;
    case kTrackball:
      animation_state_ = kCameraReturning;
      break;
    case kCameraMovingAway:
    case kCameraReturning:
      return;
  }

  animation_start_time_ = zx_clock_get(ZX_CLOCK_MONOTONIC);
  UpdateAnimation(presenter, animation_start_time_);
}

bool PerspectiveDemoMode::UpdateAnimation(Presentation* presenter,
                                          uint64_t presentation_time) {
  if (animation_state_ == kDefault) {
    return false;
  }

  const float half_width = presenter->display_info().width_in_px * 0.5f;
  const float half_height = presenter->display_info().height_in_px * 0.5f;

  // Always look at the middle of the stage.
  float target[3] = {half_width, half_height, 0};

  glm::vec3 glm_up(0, 0.1, -0.9);
  glm_up = glm::normalize(glm_up);
  float up[3] = {glm_up[0], glm_up[1], glm_up[2]};

  double secs = static_cast<double>(presentation_time - animation_start_time_) /
                1'000'000'000;
  constexpr double kAnimationDuration = 1.3;
  float param = secs / kAnimationDuration;
  if (param >= 1.f) {
    param = 1.f;
    switch (animation_state_) {
      case kDefault:
        FXL_DCHECK(false);
        return false;
      case kCameraMovingAway:
        animation_state_ = kTrackball;
        break;
      case kCameraReturning: {
        animation_state_ = kDefault;

        // Switch back to ortho view, and re-enable clipping.
        float ortho_eye[3] = {half_width, half_height, 1100.f};
        presenter->camera()->SetTransform(ortho_eye, target, up);
        presenter->camera()->SetProjection(0.f);
        return true;
      }
      case kTrackball:
        break;
    }
  }
  if (animation_state_ == kCameraReturning) {
    param = 1.f - param;  // Animating back to regular position.
  }
  param = glm::smoothstep(0.f, 1.f, param);

  // TODO: kOrthoEyeDist and the values in |eye_end| below are somewhat
  // dependent on the screen size, but also the depth of the stage's viewing
  // volume (currently hardcoded in the SceneManager implementation to 1000, and
  // not available outside).  Since this is a demo feature, it seems OK for now.
  constexpr float kOrthoEyeDist = 60000;
  const float fovy = 2.f * atan(half_height / kOrthoEyeDist);
  glm::vec3 eye_start(half_width, half_height, kOrthoEyeDist);

  constexpr float kEyePanRadius = 1.01f * kOrthoEyeDist;
  constexpr float kMaxPanAngle = kPi / 4;
  float eye_end_x =
      sin(camera_pan_ * kMaxPanAngle) * kEyePanRadius + half_width;
  float eye_end_y =
      cos(camera_pan_ * kMaxPanAngle) * kEyePanRadius + half_height;

  glm::vec3 eye_end(eye_end_x, eye_end_y, 0.75f * kOrthoEyeDist);

  glm::vec3 eye_mid = glm::mix(eye_start, eye_end, 0.4f);
  eye_mid.z = 1.5f * kOrthoEyeDist;

  // Quadratic bezier.
  glm::vec3 eye = glm::mix(glm::mix(eye_start, eye_mid, param),
                           glm::mix(eye_mid, eye_end, param), param);

  presenter->camera()->SetTransform(glm::value_ptr(eye), target, up);
  presenter->camera()->SetProjection(fovy);

  return true;
}

}  // namespace root_presenter
