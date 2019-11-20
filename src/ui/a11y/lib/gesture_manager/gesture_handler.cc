// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_tap_recognizer.h"

namespace a11y {

GestureHandler::GestureHandler(GestureArena* arena) : arena_(arena) {}

bool GestureHandler::OnGesture(GestureType gesture_type, GestureArguments args) {
  switch (gesture_type) {
    case kOneFingerTap: {
      if (one_finger_tap_callback_) {
        if (args.viewref_koid && args.coordinates) {
          one_finger_tap_callback_(*args.viewref_koid, *args.coordinates);
          return true;
        }
      }
    }
    default:
      return false;
  }

  return false;
}

void GestureHandler::BindOneFingerTapAction(OnGestureCallback callback) {
  if (gesture_recognizers_.find(kOneFingerTap) == gesture_recognizers_.end()) {
    one_finger_tap_callback_ = std::move(callback);
    gesture_recognizers_[kOneFingerTap] =
        std::make_unique<OneFingerTapRecognizer>([this](GestureContext context) {
          OnGesture(kOneFingerTap,
                    {.viewref_koid = context.view_ref_koid, .coordinates = context.local_point});
        });
    arena_->Add(gesture_recognizers_[kOneFingerTap].get());
  }
}

}  // namespace a11y
