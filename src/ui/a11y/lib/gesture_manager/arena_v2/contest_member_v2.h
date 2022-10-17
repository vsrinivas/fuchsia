// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_CONTEST_MEMBER_V2_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_CONTEST_MEMBER_V2_H_

#include <lib/fit/function.h>

namespace a11y {

// Represents a |GestureRecognizerV2|'s participation in a contest.
//
// Recognizers add themselves to the arena via |GestureArenaV2::Add(GestureRecognizerV2*)|, and
// receive a |ContestMemberV2| in |OnContestStarted|.
//
// Recognizers receive updates for a gesture as long as they hold their |ContestMemberV2| instance
// and have not been defeated. They must release their |ContestMemberV2| when they no longer want
// events. Recognizers may call |Accept()| when they want to claim a win or |Reject()| when they
// want to cede the arena. Only the first call to |Accept()| or |Reject()| has any effect.
//
// If a |ContestMemberV2| is released while still contending, it automatically rejects.
//
// Contest resolution does not occur until all members have claimed a win or declared defeat. When
// resolution occurs the highest priority claimant is awarded the win. All other claimants are
// informed of their loss.
//
// The contest is reset after the winner releases its |ContestMemberV2| or if
// all members declare defeat. A subsequent interaction will start a new contest and new
// |ContestMemberV2| instances will be issued to all recognizers. Any defeated |ContestMemberV2|s
// still held have no effect. It is recommended that recognizers reset their state and release their
// |ContestMemberV2| on defeat.
//
// In the future, we may support dispatching multiple wins to recognizers that claim multiple wins
// while a longer-running recognizer eventually declares defeat. E.g., 2 single taps and a long
// press recognized after a 3x1 tap recognizer rejects due to the long press.
class ContestMemberV2 {
 public:
  // While these states are not exposed on the |ContestMemberV2| interface, they are useful for
  // implementations and for testing.
  enum class Status {
    kUndecided,
    kAccepted,
    kRejected,
  };

  virtual ~ContestMemberV2() = default;

  // Claims a win in this contest. Resolution does not occur until all members have claimed a win
  // or declared defeat, at which point the corresponding |GestureRecognizerV2| method will be
  // called.
  virtual void Accept() = 0;

  // Declares defeat in this contest. The recognizer receives a call to |OnDefeat()| before this
  // returns.
  virtual void Reject() = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_CONTEST_MEMBER_V2_H_
