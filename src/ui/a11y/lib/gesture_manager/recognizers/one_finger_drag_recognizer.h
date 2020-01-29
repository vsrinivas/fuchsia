// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_DRAG_RECOGNIZER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_DRAG_RECOGNIZER_H_

#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include "src/ui/a11y/lib/gesture_manager/arena/contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace a11y {

// OneFingerDragRecognizer class implements logic to recognize and react to one finger drag
// gestures.
//
// Minimal effort is taken towards ignoring 2-finger gestures. For feature parity, while a second
// finger is down, events will be suppressed. When it is released, the remaining pointer must be the
// original. This requirement should probably be dropped in the future.
class OneFingerDragRecognizer : public GestureRecognizer {
 public:
  // Minimum distance (in NDC) that a drag gesture must cover in order to invoke another update.
  // Value 1.f / 16 is chosen based on one finger tap recognizer maximum displacement.
  static constexpr float kMinDragDistanceForUpdate = 1.f / 16.f;

  // Minimum duration of a drag (in milliseconds).
  // This delay is intended to ensure behavioral consistency with other screen readers.
  static constexpr zx::duration kDefaultMinDragDuration = zx::msec(500);

  // Signature for various drag recognizer callback functions.
  using DragGestureCallback = fit::function<void(GestureContext)>;

  // on_drag_update: Callback invoked as new MOVE events are handled AFTER the drag gesture is
  // recognized and has won the arena. Callbacks only occur while exactly one pointer is on the
  // screen.
  //
  // on_drag_complete: Callback invoked when the drag gesture is completed (as finger is lifted from
  // screen, or after this recognizer is awarded the win if this occurs after the gesture has
  // ended).
  //
  // drag_gesture_delay: Minimum time a finger can be in contact with the screen to be considered a
  // drag. Once this delay elapses, the recognizer tries to aggressively accept the gesture in the
  // arena.
  OneFingerDragRecognizer(DragGestureCallback on_drag_update, DragGestureCallback on_drag_complete,
                          zx::duration drag_gesture_delay = kDefaultMinDragDuration);
  ~OneFingerDragRecognizer() override;

  void HandleEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) override;
  void OnWin() override;
  void OnDefeat() override;
  void OnContestStarted(std::unique_ptr<ContestMember> contest_member) override;
  std::string DebugName() const override;

 private:
  // Represents state internal to a contest, i.e. contest member, accept delay, and pointer state.
  struct Contest;

  // Helper function to handle move events (for readability to avoid nested switch statements).
  void HandleMoveEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Returns true if distance between current pointer event and event that prompted previous call to
  // update callback exceeds kMinDragDistanceForUpdate.
  bool DragDistanceExceedsUpdateThreshold(
      const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Stores the Gesture Context which is required to execute the callback.
  GestureContext gesture_context_;

  // Callback invoked as new MOVE events are handled AFTER the drag gesture is recognized.
  DragGestureCallback on_drag_update_;

  // Callback invoked when the drag gesture is completed (as finger is lifted from screen).
  DragGestureCallback on_drag_complete_;

  // Once a drag is recognized and the recognizer claims the win, it should call update callback
  // whenever the pointer location changes by a distance exceeding kMinDragDistanceForUpdate. In
  // order to enforce this update schedule, the recognizer needs to maintain state on the previous
  // update. This field stores the location of the previous update (if the recognizer is the winner)
  // OR the location of the previous pointer event ingested (if the recognizer is not yet the
  // winner).
  GestureInfo previous_update_location_info_;

  // Minimum time a finger can be in contact with the screen to be considered a drag.
  zx::duration drag_gesture_delay_;

  std::unique_ptr<Contest> contest_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_DRAG_RECOGNIZER_H_
