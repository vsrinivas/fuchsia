// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_RECOGNIZER_V2_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_RECOGNIZER_V2_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <memory>
#include <string>

namespace a11y {

class ParticipationTokenInterface;

// A Gesture Recognizer interface for accessibility services.
//
// Gesture Recognizers provide semantic meaning to a sequence of raw pointer events by defining and
// detecting a gesture. Recognizers are added to an arena, which manages which recognizer will be
// the winner for that contest. The lifecycle of a recognizer could be simplified as follows:
//     - The recognizer adds itself to the arena via GestureArenaV2::Add().
//     - As an interaction with the touch screen happens, it will first receive a
//     |ParticipationToken| object from |OnContestStarted|, which allows the recognizer to subscribe
//     to events and mark acceptance or rejection.
//     - Recognizers can then have three main states: undecided, accepted, or rejected.
//     - The recognizer declares its intention via the |ParticipationToken|. Rejects are handled
//     immediately, while accepts may be awarded win or defeat by the arena.
//     - This recognizer will continue receiving pointer events until it releases the
//     |ParticipationToken| or is defeated. A new contest starts on the first interaction after the
//     winner releases its |ParticipationToken|. (Or the first interaction after all recognizers are
//     defeated.)
class GestureRecognizerV2 {
 public:
  virtual ~GestureRecognizerV2();

  // This method gets called when the recognizer has won the arena. The default implementation does
  // nothing.
  virtual void OnWin();

  // This method gets called when the recognizer has lost the arena. The default implementation does
  // nothing.
  virtual void OnDefeat();

  // This method gets called when the arena starts a new contest.
  virtual void OnContestStarted(
      std::unique_ptr<ParticipationTokenInterface> participation_token) = 0;

  // Non-defeated recognizers holding a |ParticipationToken| will receive a call to |HandleEvent|
  // whenever a new pointer event arrives in the arena.
  //
  // The implementation should eventually call a method on the provided |ParticipationToken| to
  // indicate when it accepts or rejects the gesture. Additionally, it should release the
  // |ParticipationToken| when it no longer wants to receive events.
  virtual void HandleEvent(
      const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) = 0;

  // A human-readable string name for the recognizer to be used in logs only, e.g. OneTapRecognizer.
  virtual std::string DebugName() const = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_RECOGNIZER_V2_H_
