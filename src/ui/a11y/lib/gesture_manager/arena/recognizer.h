// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_RECOGNIZER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_RECOGNIZER_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <string>

namespace a11y {

// A Gesture Recognizer interface for accessibility services.
//
// Gesture Recognizers provide semantic meaning to a sequence of raw pointer
// events by defining and detecting a gesture. Recognizers are added to an
// arena, which manages which recognizer will be the winner for that contending.
// The life cycle of a recognizer could be simplified as follows:
// - The recognizer adds itself to the arena via GestureArena::Add(), and holds
// to the returned pointer to an ArenaMember.
// - As an interaction with the touch screen happens, it will receive calls to
// HandleEvent().
// - Recognizers can have then four main states: not started, possible gesture,
// not possible, detected.
// - The recognizer can declare a win or defeat via the ArenaMember depending in which state it is.
// The arena itself can also declare this recognizer a win or defeat.
// - If the winner, this recognizer will continue receiving pointer events until
// it calls ArenaMember::StopRoutingPointerEvents(), indicating that it has
// finishing processing the current interaction.
class GestureRecognizer {
 public:
  virtual ~GestureRecognizer();

  // This method gets called when the recognizer has won the arena.
  virtual void OnWin() = 0;

  // This method gets called when the recognizer has lost the arena.
  virtual void OnDefeat() = 0;

  // Active recognizers in the arena will receive a call to HandleEvent whenever
  // a new pointer event arrives in the arena. If this recognizer is the winner,
  // it will continue getting new pointer events exclusively until it calls
  // ArenaMember::StopRoutingPointerEvents(), which causes the arena to be
  // restarted.
  virtual void HandleEvent(
      const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) = 0;

  // A human-readable string name for the recognizer to be used in logs only, e.g. OneTapRecognizer.
  virtual std::string DebugName() = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_RECOGNIZER_H_
