// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_RECOGNIZER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_RECOGNIZER_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <string>

namespace a11y {

class ArenaMember;

// A Gesture Recognizer interface for accessibility services.
//
// Gesture Recognizers provide semantic meaning to a sequence of raw pointer
// events by defining and detecting a gesture. Recognizers are added to an
// arena, which manages which recognizer will be the winner for that contest.
// The life cycle of a recognizer could be simplified as follows:
//     - The recognizer adds itself to the arena via GestureArena::Add(), and
//     holds to the returned pointer to an ArenaMember.
//     - As an interaction with the touch screen happens, it will receive calls
//     to HandleEvent().
//     - Recognizers can have then four main states: not started, possible
//     gesture, not possible, detected.
//     - The recognizer can declare a win or defeat via the ArenaMember
//     depending in which state it is. The arena itself can also declare this
//     recognizer a win or defeat.
//     - If the winner, this recognizer will continue receiving pointer events
//     until the interaction is over. If it wants to see another interaction, it
//     must call ArenaMember::Hold(), followed by ArenaMember::Release(), when
//     it is done.
class GestureRecognizer {
 public:
  virtual ~GestureRecognizer();

  // Adds |arena_member| to the recognizer. This is normally obtained by adding
  // the recognizer to the arena via GestureArena::Add(). For testing purposes,
  // this method can take a MockArenaMember, which simplifies writing unit tests
  // for concrete implementations of this class.
  void AddArenaMember(ArenaMember* arena_member);

  // This method gets called when the recognizer has won the arena. The default implementation does
  // nothing.
  virtual void OnWin();

  // This method gets called when the recognizer has lost the arena. The default implementation does
  // nothing.
  virtual void OnDefeat();

  // This method gets called when the arena starts a new contest. The default implementation does
  // nothing.
  virtual void OnContestStarted();

  // Active recognizers in the arena will receive a call to HandleEvent whenever
  // a new pointer event arrives in the arena. It will continue to receive
  // pointer events until the end of this interaction. If the recognizer is
  // defeated by the arena, it continues to receive pointer events until it
  // calls ArenaMember::Reject(). If a recognizer wants to continue receiving
  // pointer events of a subsequent interaction, it must call
  // ArenaMember::Hold(), before the end of the first interaction. When it is
  // done, it should call ArenaMember::Release().
  virtual void HandleEvent(
      const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) = 0;

  // A human-readable string name for the recognizer to be used in logs only, e.g. OneTapRecognizer.
  virtual std::string DebugName() const = 0;

 protected:
  ArenaMember* arena_member_ = nullptr;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_RECOGNIZER_H_
