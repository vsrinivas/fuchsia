// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"

#include <src/lib/fxl/logging.h>

namespace a11y {

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

}  // namespace a11y
