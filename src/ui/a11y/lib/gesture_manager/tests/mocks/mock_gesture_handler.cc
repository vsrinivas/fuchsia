// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/tests/mocks/mock_gesture_handler.h"

namespace accessibility_test {

bool MockGestureHandler::BindOneFingerSingleTapAction(OnGestureCallback callback) {
  bound_gestures_.push_back(GestureType::kOneFingerSingleTap);
  gesture_handlers_[GestureType::kOneFingerSingleTap] = {.on_complete = std::move(callback)};
  return true;
}

bool MockGestureHandler::BindOneFingerDoubleTapAction(OnGestureCallback callback) {
  bound_gestures_.push_back(GestureType::kOneFingerDoubleTap);
  gesture_handlers_[GestureType::kOneFingerDoubleTap] = {.on_complete = std::move(callback)};
  return true;
}

bool MockGestureHandler::BindOneFingerDragAction(OnGestureCallback on_start,
                                                 OnGestureCallback on_update,
                                                 OnGestureCallback on_complete) {
  bound_gestures_.push_back(GestureType::kOneFingerDrag);
  gesture_handlers_[GestureType::kOneFingerDrag] = {.on_start = std::move(on_start),
                                                    .on_update = std::move(on_update),
                                                    .on_complete = std::move(on_complete)};
  return true;
}

bool MockGestureHandler::BindSwipeAction(OnGestureCallback callback, GestureType gesture_type) {
  bound_gestures_.push_back(gesture_type);
  gesture_handlers_[gesture_type] = {.on_complete = std::move(callback)};
  return true;
}

void MockGestureHandler::TriggerGesture(GestureType gesture_type) {
  auto it = gesture_handlers_.find(gesture_type);
  FX_DCHECK(it != gesture_handlers_.end());
  // These values are not important and are here so that the callback can be invoked.
  if (it->second.on_start) {
    it->second.on_start(ZX_KOID_INVALID, {.x = 1, .y = 1});
  }
  if (it->second.on_update) {
    it->second.on_update(ZX_KOID_INVALID, {.x = 1, .y = 1});
  }
  if (it->second.on_complete) {
    it->second.on_complete(ZX_KOID_INVALID, {.x = 1, .y = 1});
  }
}

bool MockGestureHandler::BindTwoFingerSingleTapAction(OnGestureCallback callback) {
  bound_gestures_.push_back(GestureType::kTwoFingerSingleTap);
  gesture_handlers_[GestureType::kTwoFingerSingleTap] = {.on_complete = std::move(callback)};
  return true;
}

}  // namespace accessibility_test
