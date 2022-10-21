// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_PARTICIPATION_TOKEN_INTERFACE_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_PARTICIPATION_TOKEN_INTERFACE_H_

#include <lib/fit/function.h>

namespace a11y {

// Represents a |GestureRecognizerV2|'s participation in a contest.
//
// Recognizers add themselves to the arena via |GestureArenaV2::Add(GestureRecognizerV2*)|,
// and receive a |ParticipationToken| in |OnContestStarted|.
//
// Recognizers receive updates for a gesture as long as they hold their |ParticipationToken|
// and have not been defeated. They must release their |ParticipationToken| when they no longer
// want events. Recognizers may call |Accept()| to try to claim a win or |Reject()| to be defeated.
// Only the first call to |Accept()| or |Reject()| has any effect.
//
// If a |ParticipationToken| is released before |Accept()| or |Reject()|, it automatically rejects.
//
// Contest resolution does not occur until all recognizers have accepted or rejected. When
// resolution occurs, the highest priority "accept" is awarded the win. All others are
// informed of their loss.
//
// The contest is reset after the winner releases its |ParticipationToken| or if
// all recognizers declare defeat. A subsequent interaction will start a new contest and new
// |ParticipationToken|s will be issued to all recognizers.
class ParticipationTokenInterface {
 public:
  virtual ~ParticipationTokenInterface() = default;

  // Try to claim a win in this contest. Resolution does not occur until all recognizers have
  // accepted or rejected, at which point the corresponding |GestureRecognizerV2| method will be
  // called.
  virtual void Accept() = 0;

  // Declares defeat in this contest. The recognizer receives a call to |OnDefeat()| before this
  // returns.
  virtual void Reject() = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_V2_PARTICIPATION_TOKEN_INTERFACE_H_
