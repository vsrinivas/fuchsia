// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_INTERACTION_CONTEXT_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_INTERACTION_CONTEXT_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <map>

#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"

namespace a11y {

class GestureHandler;

// An InteractionContext holds additional data an Interaction needs to classify gestures.
class InteractionContext {
 public:
  InteractionContext(GestureHandler* gesture_handler);
  ~InteractionContext() = default;

  GestureHandler* gesture_handler() { return gesture_handler_; }

  // Resets the state of the context for a new Interaction.
  // Right now, this only clears all cached Accessibility Pointer Events.
  void Reset();

  // Adds an Accessibility Pointer Event to the context.
  void AddPointerEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event);

  // Helper method to get the latest added Accessibility Pointer Event.
  const fuchsia::ui::input::accessibility::PointerEvent* LastAddedEvent() const {
    return last_added_event_;
  }

 private:
  GestureHandler* gesture_handler_;

  // A cache of Accessibility Pointer Events keyed by pointer_id. This assumes
  // that there is only one |device_id|, hence unique pointer IDs.
  std::map<input::Gesture::PointerId, std::vector<fuchsia::ui::input::accessibility::PointerEvent>>
      pointer_events_;

  // A pointer to the last added Accessibility Pointer Event.
  fuchsia::ui::input::accessibility::PointerEvent* last_added_event_ = nullptr;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_INTERACTION_CONTEXT_H_
