// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_THREE_FINGER_SWIPE_ACTION_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_THREE_FINGER_SWIPE_ACTION_H_

#include <fuchsia/accessibility/gesture/cpp/fidl.h>
#include <lib/fit/scope.h>

#include "src/ui/a11y/lib/gesture_manager/gesture_listener_registry.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"

namespace a11y {
// This class implements three finger swipe action.
// Responsibilities of three finger swipe Action is:
//   * Calls OnGesture() for |gesture_type_| on the Gesture listener.
//     As part of the callback from OnGesture() if an utterance is returned then call TTS to speak.
// This Action is different than 1 Finger swipe, since it uses GestureListener to complete the
// action.
class ThreeFingerSwipeAction : public ScreenReaderAction {
 public:
  // |action_context| and |screen_reader_context| are expected to be be initialized and they must
  // outlive this class. This class doesn't take ownership of the pointers which are passed.
  ThreeFingerSwipeAction(ActionContext* action_context, ScreenReaderContext* screen_reader_context,
                         GestureListenerRegistry* gesture_listener_registry,
                         fuchsia::accessibility::gesture::Type gesture_type);
  ~ThreeFingerSwipeAction() override;

  // This method implements the actual sequence of events that should
  // happens when an associated gesture is performed on an element.
  void Run(GestureContext gesture_context) override;

 private:
  // Pointer to Gesture Listener Registry.
  GestureListenerRegistry* gesture_listener_registry_;

  // Stores which swipe action is being handled.
  fuchsia::accessibility::gesture::Type gesture_type_;

  fit::scope scope_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_THREE_FINGER_SWIPE_ACTION_H_
