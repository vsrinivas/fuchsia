// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/any_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"

namespace a11y {

namespace {

// This recognizer is stateless and trivial, so it makes sense as static.
AnyRecognizer consume_all;

}  // namespace

GestureHandler::GestureHandler(AddRecognizerToArenaCallback add_recognizer_callback)
    : add_recognizer_callback_(std::move(add_recognizer_callback)) {}

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
    static constexpr int number_of_taps = 1;
    gesture_recognizers_[kOneFingerTap] = std::make_unique<OneFingerNTapRecognizer>(
        [this](GestureContext context) {
          OnGesture(kOneFingerTap,
                    {.viewref_koid = context.view_ref_koid, .coordinates = context.local_point});
        },
        number_of_taps);
    add_recognizer_callback_(gesture_recognizers_[kOneFingerTap].get());
  }
}

void GestureHandler::ConsumeAll() { add_recognizer_callback_(&consume_all); }

}  // namespace a11y
