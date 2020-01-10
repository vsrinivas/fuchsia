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

void GestureHandler::OnGesture(GestureType gesture_type, GestureArguments args) {
  switch (gesture_type) {
    case kOneFingerSingleTap:
    case kOneFingerDoubleTap:
      if (gesture_callback_map_.count(gesture_type)) {
        if (args.viewref_koid && args.coordinates) {
          gesture_callback_map_.at(gesture_type)(*args.viewref_koid, *args.coordinates);
          return;
        }
      } else {
        FX_LOGS(INFO) << "No action found for GestureType:" << gesture_type;
      }
      break;

    default:
      break;
  }
}

bool GestureHandler::BindOneFingerSingleTapAction(OnGestureCallback callback) {
  return BindOneFingerTapAction(std::move(callback), kOneFingerSingleTap, 1);
}
bool GestureHandler::BindOneFingerDoubleTapAction(OnGestureCallback callback) {
  return BindOneFingerTapAction(std::move(callback), kOneFingerDoubleTap, 2);
}

bool GestureHandler::BindOneFingerTapAction(OnGestureCallback callback, GestureType gesture_type,
                                            int number_of_taps) {
  if (gesture_recognizers_.find(gesture_type) != gesture_recognizers_.end()) {
    FX_LOGS(ERROR) << "Gesture already exist for GestureType: " << gesture_type;
    return false;
  }
  gesture_callback_map_[gesture_type] = std::move(callback);

  gesture_recognizers_[gesture_type] = std::make_unique<OneFingerNTapRecognizer>(
      [this, gesture_type](GestureContext context) {
        OnGesture(gesture_type,
                  {.viewref_koid = context.view_ref_koid, .coordinates = context.local_point});
      },
      number_of_taps);
  add_recognizer_callback_(gesture_recognizers_[gesture_type].get());

  return true;
}

void GestureHandler::ConsumeAll() { add_recognizer_callback_(&consume_all); }

}  // namespace a11y
