// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_DRAG_RECOGNIZER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_DRAG_RECOGNIZER_H_

#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

#include "src/ui/a11y/lib/gesture_manager/arena/contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace a11y {

// OneFingerDragRecognizer class implements logic to recognize and react to one finger drag
// gestures.
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

  enum class DragGestureState {
    kNotStarted,                // No DOWN event detected yet.
    kInProgress,                // DOWN event detected.
    kIsWinner,                  // Recognizer has won arena contest.
    kDone,                      // End state.
  };

  // on_drag_update: Callback invoked as new MOVE events are handled AFTER the drag gesture is
  // recognized.
  // on_drag_cancel: Callback invoked when the drag gesture is interrupted by an unexpected event (e.g.
  // second finger DOWN event).
  // on_drag_complete: Callback invoked when the drag gesture is completed (as finger is lifted from screen).
  // drag_gesture_delay: Minimum time a finger can be in contact with the screen to be considered a drag. Once
  // this delay elapses, the recognizer tries to aggressively accept the gesture in the arena.
  OneFingerDragRecognizer(DragGestureCallback on_drag_update, DragGestureCallback on_drag_cancel,
                          DragGestureCallback on_drag_complete, zx::duration drag_gesture_delay);

  // Processes incoming pointer events.
  void HandleEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) override;

  // This method gets called when the recognizer has won the arena contest.
  // It sets the gesture state to kIsWinner.
  void OnWin() override;

  // This method gets called when the recognizer has lost the arena contest.
  // It resets the state of the recognizer.
  void OnDefeat() override;

  void OnContestStarted(std::unique_ptr<ContestMember> contest_member) override;

  // A human-readable string name for the recognizer to be used in logs only.
  std::string DebugName() const override { return "one_finger_drag_recognizer"; }

 protected:
  // Helper function to handle move events (for readability to avoid nested switch statements).
  void HandleMoveEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Helper function to reset the state of all the variables.
  void ResetState();

  // Helper function to reject a gesture in the arena and reset the state of the recognizer.
  void AbandonGesture();

  // Helper function to called after the drag delay period elapses. Sets drag_delay_elapsed_ to true,
  // attempts to claim arena win (if not already the winner), and invokes update callback.
  void OnDragDelayComplete();

  // Returns true if distance between current pointer event and event that prompted previous call to
  // update callback exceeds kMinDragDistanceForUpdate.
  bool DragDistanceExceedsUpdateThreshold(
      const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Stores the Gesture Context which is required to execute the callback.
  GestureContext gesture_context_;

  // Callback invoked as new MOVE events are handled AFTER the drag gesture is recognized.
  DragGestureCallback on_drag_update_;

  // Callback invoked when the drag gesture is interrupted by an unexpected event (e.g. second
  // finger DOWN event).
  DragGestureCallback on_drag_cancel_;

  // Callback invoked when the drag gesture is completed (as finger is lifted from screen).
  DragGestureCallback on_drag_complete_;

  // Async task used to scheduled gesture timeout.
  async::TaskClosureMethod<OneFingerDragRecognizer, &OneFingerDragRecognizer::OnDragDelayComplete>
      claim_win_task_;

  // Indicates state of gesture recognizer.
  DragGestureState gesture_state_ = DragGestureState::kNotStarted;

  // Indicates whether the delay period that must elapse prior to the first update has passed.
  bool drag_delay_elapsed_;

  // Once a drag is recognized and the recognizer claims the win, it should call update callback
  // whenever the pointer location changes by a distance exceeding kMinDragDistanceForUpdate. In
  // order to enforce this update schedule, the recognizer needs to maintain state on the previous
  // update. This field stores the location of the previous update (if a drag has been detected and
  // the recognizer is the winner) OR the location of the previous pointer event ingested (if a drag
  // has NOT yet been detected OR the recognizer is not yet the winner).
  GestureInfo previous_update_location_info_;

  // Minimum time a finger can be in contact with the screen to be considered a drag.
  zx::duration drag_gesture_delay_;

  std::unique_ptr<ContestMember> contest_member_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_DRAG_RECOGNIZER_H_
