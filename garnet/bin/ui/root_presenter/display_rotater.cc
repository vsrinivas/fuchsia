// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/display_rotater.h"

#include "garnet/bin/ui/root_presenter/presentation.h"

namespace root_presenter {

DisplayRotater::DisplayRotater() : spring_(0.f) {}

bool DisplayRotater::OnEvent(const fuchsia::ui::input::InputEvent& event,
                             Presentation* presentation) {
  if (event.is_keyboard()) {
    const fuchsia::ui::input::KeyboardEvent& kbd = event.keyboard();
    const uint32_t kVolumeDownKey = 232;
    if (kbd.modifiers == 0 &&
        kbd.phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED &&
        kbd.code_point == 0 && kbd.hid_usage == kVolumeDownKey) {
      FlipDisplay(presentation);
      return true;
    }
  }

  return false;
}

void DisplayRotater::SetDisplayRotation(Presentation* p,
                                        float display_rotation_degrees,
                                        bool animate) {
  if (animate) {
    float animation_start_time;

    if (spring_.is_done()) {
      animation_start_time = zx_clock_get_monotonic();
    } else {
      animation_start_time = last_animation_update_time_;
    }
    last_animation_update_time_ = animation_start_time;

    spring_.SetTargetValue(display_rotation_degrees);
    UpdateAnimation(p, animation_start_time);
  } else {
    p->display_rotation_desired_ = display_rotation_degrees;
    p->ApplyDisplayModelChanges(false, true);
  }
}

void DisplayRotater::FlipDisplay(Presentation* p) {
  if (spring_.target_value() == 0.f) {
    SetDisplayRotation(p, 180.f, true);
  } else {
    SetDisplayRotation(p, 0.f, true);
  }
}

bool DisplayRotater::UpdateAnimation(Presentation* p,
                                     uint64_t presentation_time) {
  if (spring_.is_done()) {
    return false;
  }

  float seconds_since_last_frame =
      (presentation_time - last_animation_update_time_) / 1'000'000'000.f;
  last_animation_update_time_ = presentation_time;

  spring_.ElapseTime(seconds_since_last_frame);
  p->display_rotation_desired_ = spring_.GetValue();

  p->ApplyDisplayModelChanges(false, true);
  return true;
}

glm::vec2 DisplayRotater::RotatePointerCoordinates(Presentation* p, float x,
                                                   float y) {
  // TODO(SCN-911): This math is messy and hard to understand. Instead, we
  // should just walk down the layer and scene graph and apply transformations.
  // On the other hand, this method is only used when capturing touch events,
  // which is something we intend to deprecate anyway.

  const float anchor_w =
      p->display_model_actual_.display_info().width_in_px / 2;
  const float anchor_h =
      p->display_model_actual_.display_info().height_in_px / 2;
  const int32_t startup_rotation = p->display_startup_rotation_adjustment_;
  const float current_rotation = p->display_rotation_current_;
  const float rotation = current_rotation - startup_rotation;

  glm::vec4 pointer_coords(x, y, 0.f, 1.f);
  glm::vec4 rotated_coords =
      glm::translate(glm::vec3(anchor_w, anchor_h, 0)) *
      glm::rotate(glm::radians<float>(rotation), glm::vec3(0, 0, 1)) *
      glm::translate(glm::vec3(-anchor_w, -anchor_h, 0)) * pointer_coords;

  if (abs(startup_rotation) % 180 == 90) {
    // If the aspect ratio is flipped, the origin needs to be adjusted too.
    int32_t sim_w = static_cast<int32_t>(p->display_metrics_.width_in_px());
    int32_t sim_h = static_cast<int32_t>(p->display_metrics_.height_in_px());
    float adjust_origin = (sim_w - sim_h) / 2.f;
    rotated_coords =
        glm::translate(glm::vec3(-adjust_origin, adjust_origin, 0)) *
        rotated_coords;
  }

  FXL_VLOG(2) << "Pointer coordinates rotated [" << rotation << "="
              << current_rotation << "-" << startup_rotation << "]: ("
              << pointer_coords.x << ", " << pointer_coords.y << ")->("
              << rotated_coords.x << ", " << rotated_coords.y << ").";

  return glm::vec2(rotated_coords.x, rotated_coords.y);
}

}  // namespace root_presenter
