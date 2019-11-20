// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_CONTEST_MEMBER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_CONTEST_MEMBER_H_

#include <lib/fit/function.h>

namespace a11y {

// Represents a |GestureRecognizer|'s participation in a contest.
//
// Recognizers add themselves to the arena via |GestureArena::Add(GestureRecognizer*)|, and receive
// a |ContestMember| in |OnContestStarted|.
//
// Recognizers receive updates for a gesture as long as they hold their |ContestMember| instance and
// have not been defeated. They must release their |ContestMember| when they no longer want events.
// Recognizers may call |Accept()| when they want to win the arena or |Reject()| when they want to
// cede the arena.
//
// If a |ContestMember| is released while contending, it becomes passive and can only be awared a
// win by sweep.
//
// For a group of recognizers in an arena, it is also true:
// 1. Multiple recognizers are kContending -> One becomes kWinner, remainder kDefeated.
// 2. Multiple recognizers are kContending -> All but the last declare kDefeated, the last is
// assigned kWinner.
// 3. The winner can also declare defeat by calling Reject(), which causes the arena to be empty.
//
// Contending |ContestMember|s hold the contest open. If the winning |ContestMember| is released, a
// subsequent interaction starts a new contest and new |ContestMember| instances will be issued to
// all recognizers. Any defeated |ContestMember|s still held have no effect. It is recommended that
// recognizers reset their state and release their |ContestMember| on defeat.
class ContestMember {
 public:
  enum class Status {
    kContending,  // Competing to handle the gesture.
    kWinner,      // Won the arena for the gesture.
    kDefeated,    // Lost the arena for this gesture.
    kObsolete,    // Contest over.
  };

  virtual ~ContestMember() = default;

  // Returns the status of this |ContestMember| in the contest.
  virtual Status status() const = 0;

  // Claims a win in this contest. If this results in this recognizer winning, the recognizer
  // receives a call to |OnWin()|. Returns true if this recognizer has won, whether due to this
  // claim or if it has already won, and false if it has already lost or the arena has been
  // destroyed.
  virtual bool Accept() = 0;

  // Declares defeat in this contest. If this results in this recognizer being defeated, the
  // recognizer receives a call to |OnDefeat()|.
  virtual void Reject() = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_CONTEST_MEMBER_H_
