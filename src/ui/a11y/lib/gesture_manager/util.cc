// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/util.h"

namespace a11y {

fuchsia::ui::input::PointerEvent ToPointerEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& a11y_event) {
  return {.event_time = a11y_event.event_time(),
          .device_id = a11y_event.device_id(),
          .pointer_id = a11y_event.pointer_id(),
          // Accessibility Pointer Events are only touch for now.
          .type = fuchsia::ui::input::PointerEventType::TOUCH,
          .phase = a11y_event.phase(),
          // Please note that for detecting a gesture, normalized device coordinates are used.
          // Later, if necessary, local coordinates are sent.
          .x = a11y_event.ndc_point().x,
          .y = a11y_event.ndc_point().y};
}

}  // namespace a11y
