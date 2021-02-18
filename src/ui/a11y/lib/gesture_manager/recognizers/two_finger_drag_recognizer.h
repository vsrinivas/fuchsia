// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_TWO_FINGER_DRAG_RECOGNIZER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_TWO_FINGER_DRAG_RECOGNIZER_H_

#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include "src/ui/a11y/lib/gesture_manager/arena/contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace a11y {

// TwoFingerDragRecognizer class implements logic to recognize and react to one finger drag
// gestures.
//
// Minimal effort is taken towards ignoring 2-finger gestures. For feature parity, while a second
// finger is down, events will be suppressed. When it is released, the remaining pointer must be the
// original. This requirement should probably be dropped in the future.
class TwoFingerDragRecognizer : public GestureRecognizer {
 public:
  // Minimum duration of a drag (in milliseconds).
  // This delay is intended to ensure behavioral consistency with other screen readers.
  static constexpr zx::duration kDefaultMinDragDuration = zx::msec(500);

  // Displacements of less than 1/16 are considered valid for taps, so we want
  // to recognize slightly larger gestures as drags.
  static constexpr float kDragDisplacementThreshold = 1.f / 10;

  // If the distance between the two fingers changes by more than 20%, we can
  // accept this gesture as a drag.
  static constexpr float kFingerSeparationThresholdFactor = 6.f / 5;

  // Maximum allowable time elapsed between the first and second fingers' DOWN events.
  static constexpr zx::duration kMaxSecondFingerDownDelay = zx::msec(300);

  // Signature for various drag recognizer callback functions.
  using DragGestureCallback = fit::function<void(GestureContext)>;

  // on_drag_started: Callback invoked at most once when the recognizer has won the arena. Callback
  // only occurs if at least one pointer is on the screen.
  //
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
  TwoFingerDragRecognizer(DragGestureCallback on_drag_started, DragGestureCallback on_drag_update,
                          DragGestureCallback on_drag_complete,
                          zx::duration drag_gesture_delay = kDefaultMinDragDuration);
  ~TwoFingerDragRecognizer() override;

  void HandleEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) override;
  void OnWin() override;
  void OnDefeat() override;
  void OnContestStarted(std::unique_ptr<ContestMember> contest_member) override;
  std::string DebugName() const override;

 private:
  // Represents state internal to a contest, i.e. contest member, accept delay, and pointer state.
  struct Contest;

  // Resets gesture_context_ and contest_.
  void ResetRecognizer();

  // Helper function to handle move events (for readability to avoid nested switch statements).
  void HandleMoveEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Returns true if the dispalcement between the gesture's starting and current
  // centroids exceeds kDragDisplacementThreshold.
  bool DisplacementExceedsThreshold();

  // Returns true if the distance between the two fingers has changed by
  // kFingerSeparationThresholdFactor relative to the start of the gesture.
  bool SeparationExceedsThreshold();

  // Callback invoked once the drag gesture has been recognized.
  DragGestureCallback on_drag_started_;

  // Callback invoked as new MOVE events are handled AFTER the drag gesture is recognized.
  DragGestureCallback on_drag_update_;

  // Callback invoked when the drag gesture is completed (as finger is lifted from screen).
  DragGestureCallback on_drag_complete_;

  GestureContext gesture_context_;

  // Minimum time a finger can be in contact with the screen to be considered a drag.
  zx::duration drag_gesture_delay_;

  std::unique_ptr<Contest> contest_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_TWO_FINGER_DRAG_RECOGNIZER_H_
