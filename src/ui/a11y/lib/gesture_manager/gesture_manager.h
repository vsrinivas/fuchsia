// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <memory>

#include "lib/fidl/cpp/binding.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"

namespace a11y {

// A Gesture manager to listen for incoming pointer events and call actions
// associated with detected gestures.
class GestureManager : public fuchsia::ui::input::accessibility::PointerEventListener {
 public:
  GestureManager();

  fidl::Binding<fuchsia::ui::input::accessibility::PointerEventListener>& binding() {
    return binding_;
  }

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
  void OnEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event) override;

  // Returns a pointer to the gesture handler, which can be used to bind actions to gestures.
  GestureHandler* gesture_handler() { return &gesture_handler_; }

  GestureArena* arena() { return &arena_; }

 private:
  // Adds GestureRecognizer to gesture arena.
  void AddRecognizer(GestureRecognizer* recognizer);

  // Binding to the listener implemented by this class. This object is owned
  // here instead in an external BindingSet so that FIDL events can be called.
  fidl::Binding<fuchsia::ui::input::accessibility::PointerEventListener> binding_;

  // Manages bound actions and gestures.
  GestureHandler gesture_handler_;

  // An arena to manage contending of pointer events across multiple gesture recognizers.
  // arena_ should be instantiated after gesture_hander_, since arena_ depends on recognizers owned
  // by handler_.
  // TODO(fxbug.dev/43223): Recognizer pointers should be owned by a single class. Other users of
  // these pointers should be able to validate pointers before use.
  GestureArena arena_;
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_H_
