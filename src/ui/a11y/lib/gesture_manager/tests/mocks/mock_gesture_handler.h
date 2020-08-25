// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_TESTS_MOCKS_MOCK_GESTURE_HANDLER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_TESTS_MOCKS_MOCK_GESTURE_HANDLER_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"

namespace accessibility_test {

class MockGestureHandler : public a11y::GestureHandler {
 public:
  MockGestureHandler() = default;
  ~MockGestureHandler() = default;

  // |GestureHandler|
  bool BindOneFingerSingleTapAction(OnGestureCallback callback) override;
  bool BindOneFingerDoubleTapAction(OnGestureCallback callback) override;
  bool BindOneFingerDragAction(OnGestureCallback on_start, OnGestureCallback on_update,
                               OnGestureCallback on_complete) override;
  bool BindSwipeAction(OnGestureCallback callback, GestureType gesture_type) override;
  bool BindTwoFingerSingleTapAction(OnGestureCallback callback) override;

  std::vector<GestureType>& bound_gestures() { return bound_gestures_; }

  void TriggerGesture(GestureType gesture_type);

 private:
  // Holds the gestures bound to the handler, in order of registration.
  std::vector<GestureType> bound_gestures_;

  // Holds the handler for each gesture type.
  std::unordered_map<GestureType, GestureEventHandlers> gesture_handlers_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_TESTS_MOCKS_MOCK_GESTURE_HANDLER_H_
