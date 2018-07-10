// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_TALKBACK_GESTURE_DETECTOR_H_
#define GARNET_BIN_A11Y_TALKBACK_GESTURE_DETECTOR_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/bin/a11y/talkback/talkback_impl.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace talkback {

// Max duration between a touch up and a second touch down to register
// a double tap. Time in milliseconds.
constexpr zx::duration kTapDelay = zx::msec(120);
// Duration to wait between the first touch down and first touch up
// to distinguish between a short tap and long press. Time in nanoseconds.
constexpr  uint64_t kLongPressDelay = zx::msec(120).to_nsecs();
// Duration to wait after registering a touch move event during a finger move
// or long press before registering another touch move event. This is done
// to limit the rate of move calls made while a finger is held on the screen.
// Time in nanoseconds.
constexpr uint64_t kMoveCallDelay = zx::msec(17).to_nsecs();

// Talkback gesture detector that takes in raw inputs to process.
// The gestures it detects:
// - Single tap - a11y focus set on element touched.
// - One finger slide - a11y focus set to where currently touching.
// - Double tap - Apply a11y tap action to current a11y focused node.
// - Two finger slide - Simulate full touch events for finger #1
//   that is on the screen.
class GestureDetector {
 public:
  explicit GestureDetector(component::StartupContext* startup_context,
                           TalkbackImpl* talkback);
  ~GestureDetector() = default;

 private:
  enum class State {
    kIdle = 0,             // No fingers on screen
    kFirstTouchDown = 1,   // Tap started
    kFirstTouchUp = 2,     // Full tap finished
    kSecondTouchDown = 4,  // Double tap started
    kTwoFingersDown = 5,   // Two fingers are on the screen
  };

  // Fidl event listener functions for fuchsia::accessibility::InputDispatcher.

  // Handle the input event depending on the current state of
  // the gesture detector. We break up the handler for each state
  // into individual functions below.
  void OnInputEvent(fuchsia::ui::input::PointerEvent event);

  // Resets the state to idle once a new presentation is displayed; input
  // from previous presentations should not extend into new ones. Also stores
  // the current ViewTreeToken to have a reference of which view tree to
  // perform hit tests.
  void OnPresentationChangedEvent(fuchsia::ui::viewsv1::ViewTreeToken token);

  // Executed |kTapDelay| ms after a touch up. If a new touch down did not
  // appear in this time period, we treat this as a single tap. If a new touch
  // down does appear, then we treat this event as a double-tap instead of a
  // single-tap.
  // After a single tap, the item under finger on touch up gains
  // accessibility focus and the state is returned to |kIdle|.
  void AfterTapDelay();

  // Called when state is |kIdle|.
  // If a finger touches down, the state moves to |kFirstTouchDown|.
  void FromIdle(fuchsia::ui::input::PointerEvent event);

  // Called when state is |kFirstTouchDown|.
  // While finger #1 is down, we set accessibility focus at the current
  // position after |kLongPressDelay| ms after the touch down event. The delay
  // is added to keep in line with the delay before checking for a tap. This
  // provides functionality to explore UI with a finger moving around the
  // screen without lifting up.
  //
  // If finger #1 is lifted:
  // We return to |kIdle| if it happened |kLongPressDelay| ms after touching
  // down (We do not treat long presses as taps).
  // Otherwise, state is set to |kFirstTouchDown|, to continue checking
  // for a double tap. We also launch an AfterTapDelay call  We use delayed
  // async call to AfterTapDelay to detect single taps if the delay is too long.
  // This is necessary because taps can lead to a second touch down quickly
  // afterwards if the user wishes to double tap.
  //
  // If finger #2 touches the screen, we move to |kTwoFingersDown| and
  // simulate a touch down event. The position of finger #1 on the
  // screen is always used for simulation.
  void FromFirstTouchDown(fuchsia::ui::input::PointerEvent event);

  // Called when state is |kFirstTouchUp|.
  // If a finger touches down:
  // If the delay after the first tap was too long, return to |kIdle|.
  // Otherwise, move to |kSecondTouchDown|, to signal the start of a double tap.
  void FromFirstTouchUp(fuchsia::ui::input::PointerEvent event);

  // Called when state is |kSecondTouchDown|.
  // On the second tap up, perform an a11y tap on the currently a11y focused
  // node. Return to |kIdle| afterwards.
  void FromSecondTouchDown(fuchsia::ui::input::PointerEvent event);

  // Called when state is |kTwoFingersDown|.
  // If finger #1 on the screen moves, we return the move event to
  // the regular input pipeline to simulate it.
  // When finger #1 on the screen is lifted first, we simulate a
  // touch up event, set finger #2 on the screen to be the first
  // one, and go back to |kFirstTouchDown|.
  //
  // If finger #2 on the screen is lifted first, we simulate a touch
  // up event, and only return to |kFirstTouchDown|.
  void FromTwoFingersDown(fuchsia::ui::input::PointerEvent event);

  // Sets the current state to |kIdle|. If the current state was
  // |kTwoFingersDown|, we send a simulated cancel since a simulated
  // touch down has not finished in this phase.
  void CancelAndIdle();

  // Simulates a touch down event when two fingers are on the screen by
  // sending the location of finger #1 that was registered. We
  // return two clones of the currently stored |finger1_pointer_event|
  // with PointerEventPhase ADD and DOWN in that order.
  void SimulateTouchDown();

  // Simulates a touch up event when two fingers are on the screen by
  // sending the location of finger #1 that was registered. We
  // return two clones of the currently stored |finger1_pointer_event|
  // with PointerEventPhase UP and REMOVE in that order.
  void SimulateTouchUp();

  // Simulates a touch cancel event when two fingers are on the screen by
  // sending the location of finger #1 that was registered. We
  // return a clone of the currently stored |finger1_pointer_event|
  // with PointerEventPhase CANCEL.
  void SimulateCancel();

  component::StartupContext* startup_context_;

  TalkbackImpl* talkback_;
  fuchsia::accessibility::TouchDispatcherPtr touch_dispatcher_;

  // The ViewTreeToken for the currently displayed presentation is needed
  // to perform hit-tests on views in the current view tree.
  fuchsia::ui::viewsv1::ViewTreeToken token_;

  // The current state of the gesture detector state machine. Starts in
  // the idle position.
  State state_ = State::kIdle;

  // Pointer id of finger #1 to touch the screen at a time. This is
  // necessary to ignore events from other fingers and perform checking when
  // finger #2 touches the screen.
  uint32_t finger1_pointer_id_;
  // Pointer id of finger #2 to touch the screen to detect two finger
  // scrolling. This value is only meaningful during the |kTwoFingersDown|
  // state, and replaces |finger1_pointer_id_| if finger #1 is lifted first
  // during two finger mode.
  uint32_t finger2_pointer_id_;

  // Last pointer event time for DOWN and UP events used to find time deltas
  // to check for tap/double tap/long press delays. Time in nanoseconds.
  uint64_t last_pointer_down_or_up_event_;

  // Last time for acting upon a MOVE event during a finger drag along the
  // screen or a long press.
  uint64_t last_move_call_;

  // The following two cached pointer events are used to clone simulated
  // PointerEvents while two fingers are on the screen.

  // The last pointer event received for finger #1 on the screen.
  fuchsia::ui::input::PointerEvent finger1_pointer_event_;
  // The last pointer event received for finger #2 on the screen.
  // Replaces |finger1_pointer_event| if finger #1 is
  // lifted first during two finger mode.
  fuchsia::ui::input::PointerEvent finger2_pointer_event_;

  // Needed to queue up AfterTapDelay.
  async_dispatcher_t* const tap_dispatcher_ = nullptr;
};

}  // namespace talkback

#endif  // GARNET_BIN_A11Y_TALKBACK_GESTURE_DETECTOR_H_
