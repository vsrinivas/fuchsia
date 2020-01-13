// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ACTIVITY_ACTIVITY_STATE_MACHINE_H_
#define SRC_UI_BIN_ACTIVITY_ACTIVITY_STATE_MACHINE_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <map>
#include <optional>

#include "src/lib/fxl/macros.h"

namespace activity {

inline std::ostream& operator<<(std::ostream& os, fuchsia::ui::activity::State s) {
  switch (s) {
    case fuchsia::ui::activity::State::UNKNOWN:
      return os << "UNKNOWN";
    case fuchsia::ui::activity::State::IDLE:
      return os << "IDLE";
    case fuchsia::ui::activity::State::ACTIVE:
      return os << "ACTIVE";
    default:
      os.setstate(std::ios_base::failbit);
      return os;
  }
}

// Input events.
enum Event { USER_INPUT, TIMEOUT };

inline std::ostream& operator<<(std::ostream& os, Event s) {
  switch (s) {
    case Event::USER_INPUT:
      return os << "USER_INPUT";
    case Event::TIMEOUT:
      return os << "TIMEOUT";
    default:
      os.setstate(std::ios_base::failbit);
      return os;
  }
}

// ActivityStateMachine is a state machine which take system and user activity as input and
// determine the current activity state of the system as output.
class ActivityStateMachine {
 public:
  ActivityStateMachine() = default;
  ~ActivityStateMachine() = default;

  // Provide input to the state machine.
  void ReceiveEvent(Event event);

  // Poll the current state of the state machine.
  fuchsia::ui::activity::State state() const { return state_; }

  // Returns the time in a state after which, if is no input is received, an
  // Event::TIMEOUT should be delivered to the state machine.
  // If the return is absent then the state should never receive Event::TIMEOUT.
  static std::optional<zx::duration> TimeoutFor(fuchsia::ui::activity::State state);

  // Translations from FIDL activities to internal Events.

  // Translate |activity| to an appropriate Event.
  constexpr static Event EventForDiscreteActivity(
      const fuchsia::ui::activity::DiscreteActivity& activity) {
    return Event::USER_INPUT;
  }
  // Returns an Event which should be delivered as an ongoing activity starts.
  constexpr static Event EventForOngoingActivityStart() { return Event::USER_INPUT; }
  // Returns an Event which should be delivered as an ongoing activity ends.
  constexpr static Event EventForOngoingActivityEnd() { return Event::USER_INPUT; }

  using StateTable =
      std::map<std::pair<fuchsia::ui::activity::State, Event>, fuchsia::ui::activity::State>;

 private:
  fuchsia::ui::activity::State state_ = fuchsia::ui::activity::State::IDLE;

  const static StateTable kStateTable;
  const static zx::duration kIdleDuration;

  FXL_DISALLOW_COPY_AND_ASSIGN(ActivityStateMachine);
};

}  // namespace activity

#endif  // SRC_UI_BIN_ACTIVITY_ACTIVITY_STATE_MACHINE_H_
