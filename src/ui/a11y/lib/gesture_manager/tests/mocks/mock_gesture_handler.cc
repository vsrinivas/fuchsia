// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/tests/mocks/mock_gesture_handler.h"

namespace accessibility_test {

bool MockGestureHandler::BindMFingerNTapAction(uint32_t num_fingers, uint32_t num_taps,
                                               OnGestureCallback on_recognize) {
  if (num_fingers == 1 && num_taps == 1) {
    gesture_handlers_[GestureType::kOneFingerSingleTap] = {.on_recognize = std::move(on_recognize)};
    bound_gestures_.push_back(GestureType::kOneFingerSingleTap);
    return true;
  }
  if (num_fingers == 1 && num_taps == 2) {
    gesture_handlers_[GestureType::kOneFingerDoubleTap] = {.on_recognize = std::move(on_recognize)};
    bound_gestures_.push_back(GestureType::kOneFingerDoubleTap);
    return true;
  }
  if (num_fingers == 1 && num_taps == 3) {
    gesture_handlers_[GestureType::kOneFingerTripleTap] = {.on_recognize = std::move(on_recognize)};
    bound_gestures_.push_back(GestureType::kOneFingerTripleTap);
    return true;
  }
  if (num_fingers == 2 && num_taps == 1) {
    gesture_handlers_[GestureType::kTwoFingerSingleTap] = {.on_recognize = std::move(on_recognize)};
    bound_gestures_.push_back(GestureType::kTwoFingerSingleTap);
    return true;
  }
  if (num_fingers == 3 && num_taps == 2) {
    gesture_handlers_[GestureType::kThreeFingerDoubleTap] = {.on_recognize =
                                                                 std::move(on_recognize)};
    bound_gestures_.push_back(GestureType::kThreeFingerDoubleTap);
    return true;
  }

  return false;
}

bool MockGestureHandler::BindOneFingerSingleTapAction(OnGestureCallback callback) {
  bound_gestures_.push_back(GestureType::kOneFingerSingleTap);
  gesture_handlers_[GestureType::kOneFingerSingleTap] = {.on_recognize = std::move(callback)};
  return true;
}

bool MockGestureHandler::BindTwoFingerDragAction(OnGestureCallback on_recognize,
                                                 OnGestureCallback on_update,
                                                 OnGestureCallback on_complete) {
  bound_gestures_.push_back(GestureType::kTwoFingerDrag);
  gesture_handlers_[GestureType::kTwoFingerDrag] = {.on_recognize = std::move(on_recognize),
                                                    .on_update = std::move(on_update),
                                                    .on_complete = std::move(on_complete)};
  return true;
}

bool MockGestureHandler::BindOneFingerDoubleTapAction(OnGestureCallback callback) {
  bound_gestures_.push_back(GestureType::kOneFingerDoubleTap);
  gesture_handlers_[GestureType::kOneFingerDoubleTap] = {.on_recognize = std::move(callback)};
  return true;
}

bool MockGestureHandler::BindOneFingerDragAction(OnGestureCallback on_recognize,
                                                 OnGestureCallback on_update,
                                                 OnGestureCallback on_complete) {
  bound_gestures_.push_back(GestureType::kOneFingerDrag);
  gesture_handlers_[GestureType::kOneFingerDrag] = {.on_recognize = std::move(on_recognize),
                                                    .on_update = std::move(on_update),
                                                    .on_complete = std::move(on_complete)};
  return true;
}

bool MockGestureHandler::BindSwipeAction(OnGestureCallback callback, GestureType gesture_type) {
  bound_gestures_.push_back(gesture_type);
  gesture_handlers_[gesture_type] = {.on_recognize = std::move(callback)};
  return true;
}

void MockGestureHandler::TriggerGesture(GestureType gesture_type,
                                        a11y::GestureContext gesture_context) {
  TriggerGestureRecognize(gesture_type, gesture_context);
  TriggerGestureUpdate(gesture_type, gesture_context);
  TriggerGestureComplete(gesture_type, gesture_context);
}

void MockGestureHandler::TriggerGestureRecognize(GestureType gesture_type,
                                                 a11y::GestureContext gesture_context) {
  auto it = gesture_handlers_.find(gesture_type);
  FX_DCHECK(it != gesture_handlers_.end());
  if (it->second.on_recognize) {
    it->second.on_recognize(gesture_context);
  }
}

void MockGestureHandler::TriggerGestureUpdate(GestureType gesture_type,
                                              a11y::GestureContext gesture_context) {
  auto it = gesture_handlers_.find(gesture_type);
  FX_DCHECK(it != gesture_handlers_.end());
  if (it->second.on_update) {
    it->second.on_update(gesture_context);
  }
}

void MockGestureHandler::TriggerGestureComplete(GestureType gesture_type,
                                                a11y::GestureContext gesture_context) {
  auto it = gesture_handlers_.find(gesture_type);
  FX_DCHECK(it != gesture_handlers_.end());
  if (it->second.on_complete) {
    it->second.on_complete(gesture_context);
  }
}

bool MockGestureHandler::BindTwoFingerSingleTapAction(OnGestureCallback callback) {
  bound_gestures_.push_back(GestureType::kTwoFingerSingleTap);
  gesture_handlers_[GestureType::kTwoFingerSingleTap] = {.on_recognize = std::move(callback)};
  return true;
}

bool MockGestureHandler::BindMFingerNTapDragAction(OnGestureCallback on_recognize,
                                                   OnGestureCallback on_update,
                                                   OnGestureCallback on_complete,
                                                   uint32_t num_fingers, uint32_t num_taps) {
  if (num_fingers == 1 && num_taps == 2) {
    gesture_handlers_[GestureType::kOneFingerDoubleTapDrag] = {
        .on_recognize = std::move(on_recognize),
        .on_update = std::move(on_update),
        .on_complete = std::move(on_complete)};
    bound_gestures_.push_back(GestureType::kOneFingerDoubleTapDrag);
    return true;
  }
  if (num_fingers == 1 && num_taps == 3) {
    gesture_handlers_[GestureType::kOneFingerTripleTapDrag] = {
        .on_recognize = std::move(on_recognize),
        .on_update = std::move(on_update),
        .on_complete = std::move(on_complete)};
    bound_gestures_.push_back(GestureType::kOneFingerTripleTapDrag);
    return true;
  }
  if (num_fingers == 3 && num_taps == 2) {
    bound_gestures_.push_back(GestureType::kThreeFingerDoubleTapDrag);
    gesture_handlers_[GestureType::kThreeFingerDoubleTapDrag] = {
        .on_recognize = std::move(on_recognize),
        .on_update = std::move(on_update),
        .on_complete = std::move(on_complete)};
    return true;
  }

  return false;
}

}  // namespace accessibility_test
