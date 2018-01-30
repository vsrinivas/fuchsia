// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/display_flipper.h"

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
#include <glm/gtc/constants.hpp>

#include "garnet/bin/ui/root_presenter/presentation.h"

namespace root_presenter {
namespace {

constexpr float kPi = glm::pi<float>();
}

DisplayFlipper::DisplayFlipper() {}

bool DisplayFlipper::OnEvent(const mozart::InputEventPtr& event,
                             Presentation* presentation,
                             bool* continue_dispatch_out) {
  FXL_DCHECK(continue_dispatch_out);
  bool invalidate = false;
  if (event->is_keyboard()) {
    const mozart::KeyboardEventPtr& kbd = event->get_keyboard();
    const uint32_t kVolumeDownKey = 232;
    if (kbd->modifiers == 0 &&
        kbd->phase == mozart::KeyboardEvent::Phase::PRESSED &&
        kbd->code_point == 0 && kbd->hid_usage == kVolumeDownKey) {
      FlipDisplay(presentation);
      invalidate = true;
      *continue_dispatch_out = false;
    }
  }

  return invalidate;
}

void DisplayFlipper::FlipDisplay(Presentation* p) {
  if (display_flipped_) {
    p->scene_.SetAnchor(0, 0, 0);
    p->scene_.SetRotation(0, 0, 0, 0);
    p->scene_.SetTranslation(0, 0, 0);
  } else {
    float anchor_x = p->display_metrics_.width_in_pp() / 2;
    float anchor_y = p->display_metrics_.height_in_pp() / 2;

    glm::quat display_rotation = glm::quat(glm::vec3(0, 0, kPi));

    p->scene_.SetAnchor(anchor_x, anchor_y, 0);
    p->scene_.SetRotation(display_rotation.x, display_rotation.y,
                          display_rotation.z, display_rotation.w);
    p->scene_.SetTranslation(p->display_metrics_.width_in_px() / 2 - anchor_x,
                             p->display_metrics_.height_in_px() / 2 - anchor_y,
                             0);
    p->light_direction_.x = -p->light_direction_.x;
    p->light_direction_.y = -p->light_direction_.y;
    glm::vec3 l = p->light_direction_;
    p->directional_light_.SetDirection(l.x, l.y, l.z);
  }
  display_flipped_ = !display_flipped_;
}

}  // namespace root_presenter
