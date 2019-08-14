// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <memory>

#include "src/lib/ui/input/gesture_detector.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"
#include "src/ui/a11y/lib/gesture_manager/interaction.h"
#include "src/ui/a11y/lib/gesture_manager/interaction_context.h"

namespace a11y {

// A Gesture manager to listen for incoming pointer events and call actions
// associated with detected gestures.
class GestureManager : public fuchsia::ui::input::accessibility::PointerEventListener,
                       private input::GestureDetector::Delegate {
 public:
  GestureManager();

  // |fuchsia.ui.input.accessibility.PointerEventListener|
  // Listens for an incoming pointer event. All pointer event streams are
  // consumed on an ADD event. Please see
  // |fuchsia.ui.input.accessibility.EventHandling| for more info on consuming
  // / rejecting streams.
  //
  // Events are then sent to the gesture detector, which tries to match the
  // current Interaction with a gesture. When a gesture is matched, if an action
  // is bound to handle that particular gesture, it gets called. Please
  // also see interaction.h and gesture_handler.h for more details.
  // TODO(lucasradaelli): Implement rejecting a pointer event stream.
  void OnEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event,
               OnEventCallback callback) override;

  // Returns a pointer to the gesture handler, which can be used to bind actions to gestures.
  GestureHandler* gesture_handler() { return &gesture_handler_; }

 private:
  // |input::GestureDetector::Delegate|
  // Called by the gesture detector library when a new interaction starts. This method creates a new
  // |Interaction| which has access to |context_|.
  std::unique_ptr<input::GestureDetector::Interaction> BeginInteraction(
      const input::Gesture* gesture) override;

  // Used to detect gestures and build a new Interaction.
  input::GestureDetector gesture_detector_;

  // Holds information needed by an Interaction to classify a gesture.
  InteractionContext context_;

  // Manages bound actions and gestures.
  GestureHandler gesture_handler_;
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_H_
