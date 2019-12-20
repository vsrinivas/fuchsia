// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"

#include <fuchsia/ui/input/cpp/fidl.h>

#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/util.h"

namespace a11y {

GestureManager::GestureManager()
    : binding_(this),
      gesture_handler_([this](GestureRecognizer* recognizer) { AddRecognizer(recognizer); }),
      arena_(fit::bind_member(&binding_.events(),
                              &fuchsia::ui::input::accessibility::PointerEventListener::
                                  EventSender_::OnStreamHandled)) {}

void GestureManager::OnEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event) {
  arena_.OnEvent(pointer_event);
}

void GestureManager::AddRecognizer(GestureRecognizer* recognizer) { arena_.Add(recognizer); }

}  // namespace a11y
