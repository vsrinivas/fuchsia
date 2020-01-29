// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/any_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_drag_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"

namespace a11y {

namespace {

// This recognizer is stateless and trivial, so it makes sense as static.
AnyRecognizer consume_all;

}  // namespace

GestureHandler::GestureHandler(AddRecognizerToArenaCallback add_recognizer_callback)
    : add_recognizer_callback_(std::move(add_recognizer_callback)) {}

void GestureHandler::OnGesture(GestureType gesture_type, GestureArguments args) {
  if (gesture_callback_map_.find(gesture_type) == gesture_callback_map_.end()) {
    FX_LOGS(INFO) << "GestureHandler::OnGesture: No action found for GestureType:" << gesture_type;
    return;
  }

  // TODO: Revisit which gestures need coordinates. As currently implemented,
  // all gestures expect them, but they may be unnecessary for some gestures.
  if (args.viewref_koid && args.coordinates) {
    gesture_callback_map_.at(gesture_type)(*args.viewref_koid, *args.coordinates);
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
    FX_LOGS(ERROR) << "Action already exists for GestureType: " << gesture_type;
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

bool GestureHandler::BindOneFingerDragAction(OnGestureCallback callback) {
  if (gesture_recognizers_.find(kOneFingerDrag) != gesture_recognizers_.end()) {
    FX_LOGS(ERROR) << "Action already exists for one-finger drag gesture.";
    return false;
  }

  gesture_callback_map_[kOneFingerDrag] = std::move(callback);

  gesture_recognizers_[kOneFingerDrag] = std::make_unique<OneFingerDragRecognizer>(
      [this](GestureContext context) {
        OnGesture(kOneFingerDrag,
                  {.viewref_koid = context.view_ref_koid, .coordinates = context.local_point});
      }, /* drag update callback */
      [](GestureContext context) {} /* drag completion callback */);
  add_recognizer_callback_(gesture_recognizers_[kOneFingerDrag].get());

  return true;
}

void GestureHandler::ConsumeAll() { add_recognizer_callback_(&consume_all); }

}  // namespace a11y
