// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"

#include <fuchsia/ui/input/cpp/fidl.h>

#include "src/ui/a11y/lib/gesture_manager/util.h"

namespace a11y {

GestureManager::GestureManager()
    : binding_(this),
      arena_(fit::bind_member(
          &binding_.events(),
          &fuchsia::ui::input::accessibility::PointerEventListener::EventSender_::OnStreamHandled)),
      gesture_handler_(&arena_) {}

void GestureManager::OnEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event) {
  arena_.OnEvent(pointer_event);
}

}  // namespace a11y
