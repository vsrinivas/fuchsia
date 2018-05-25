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
      animation_start_time = zx_clock_get(ZX_CLOCK_MONOTONIC);
    } else {
      animation_start_time = last_animation_update_time_;
    }
    last_animation_update_time_ = animation_start_time;

    spring_.SetTargetValue(display_rotation_degrees);
    UpdateAnimation(p, animation_start_time);
  } else {
    p->display_rotation_desired_ = display_rotation_degrees;
    if (p->ApplyDisplayModelChanges(false)) {
      p->PresentScene();
    }
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

  if (p->ApplyDisplayModelChanges(false)) {
    p->PresentScene();
  }
  return true;
}

}  // namespace root_presenter
