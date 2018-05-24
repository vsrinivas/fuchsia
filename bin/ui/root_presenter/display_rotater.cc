// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/display_rotater.h"

#include "garnet/bin/ui/root_presenter/presentation.h"

namespace root_presenter {

DisplayRotater::DisplayRotater() {}

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
  rotation_animation_start_value_ = p->display_rotation_desired_;
  rotation_animation_end_value_ = display_rotation_degrees;

  if (animate) {
    animation_start_time_ = zx_clock_get(ZX_CLOCK_MONOTONIC);
    UpdateAnimation(p, animation_start_time_);
  } else {
    p->display_rotation_desired_ = rotation_animation_end_value_;
    if (p->ApplyDisplayModelChanges(false)) {
      p->PresentScene();
    }
  }
}

void DisplayRotater::FlipDisplay(Presentation* p) {
  if (rotation_animation_end_value_ == 0.f) {
    SetDisplayRotation(p, 180.f, true);
  } else {
    SetDisplayRotation(p, 0.f, true);
  }
}

bool DisplayRotater::UpdateAnimation(Presentation* p,
                                     uint64_t presentation_time) {
  if (p->display_rotation_desired_ == rotation_animation_end_value_) {
    return false;
  }

  // Adjust duration so velocity of animation (degrees/millisecond) is the same.
  float animation_duration = std::abs(rotation_animation_end_value_ -
                                      rotation_animation_start_value_) /
                             180.f * 250'000'000;

  float t = std::min(
      1.f, (presentation_time - animation_start_time_) / animation_duration);
  p->display_rotation_desired_ = (1 - t) * rotation_animation_start_value_ +
                                 t * rotation_animation_end_value_;
  if (p->ApplyDisplayModelChanges(false)) {
    p->PresentScene();
  }
  return true;
}

}  // namespace root_presenter
