// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "src/ui/a11y/lib/gesture_manager/interaction.h"

namespace a11y {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;

InteractionContext::InteractionContext(GestureHandler* gesture_handler)
    : gesture_handler_(gesture_handler) {
  FXL_CHECK(gesture_handler_);
}

void InteractionContext::AddPointerEvent(
    fuchsia::ui::input::accessibility::PointerEvent pointer_event) {
  auto pointer_id = pointer_event.pointer_id();
  pointer_events_[pointer_id].emplace_back(std::move(pointer_event));
  last_added_event_ = &pointer_events_[pointer_id].back();
}

void InteractionContext::Reset() {
  pointer_events_.clear();
  last_added_event_ = nullptr;
}

}  // namespace a11y
