// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ACTIVITY_SERVICE_STATE_MACHINE_DRIVER_H_
#define GARNET_BIN_UI_ACTIVITY_SERVICE_STATE_MACHINE_DRIVER_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <set>

#include "garnet/bin/ui/activity_service/activity_state_machine.h"
#include "garnet/bin/ui/activity_service/common.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace activity_service {

using StateChangedCallback =
    fit::function<void(fuchsia::ui::activity::State state, zx::time transition_time)>;

// StateMachineDriver is a class which drives an ActivityStateMachine based on user activity.
//
// The responsibilities of the StateMachineDriver are:
//  - To receive inputs and forward them to the state machine in a sequential manner, and
//  - To manage timers which drive the state machine in the absence of any inputs.
//
// StateMachineDriver dispatches work onto an asynchronous loop, which ensures sequential processing
// of events from different sources (e.g. user input v.s.  automated timers).
class StateMachineDriver {
 public:
  explicit StateMachineDriver(async_dispatcher_t* dispatcher)
      : last_transition_time_(async::Now(dispatcher)),
        dispatcher_(dispatcher),
        weak_factory_(this) {}
  ~StateMachineDriver() = default;

  fuchsia::ui::activity::State state() const { return state_machine_.state(); };
  const ActivityStateMachine& state_machine() const { return state_machine_; };

  void SetStateChangedCallback(StateChangedCallback callback) {
    state_changed_callback_ = std::move(callback);
  }

  // Inputs to the state machine. These methods enqueue a work item onto the driver's async loop to
  // handle the given activity, scheduling the work item to run at |time|.
  // If |time| was before the last state transition, it is ignored and ZX_ERR_OUT_OF_BOUNDS is
  // returned. (Events may be interpreted differently depending on the current state.)
  zx_status_t ReceiveDiscreteActivity(const fuchsia::ui::activity::DiscreteActivity& activity,
                                      zx::time time);
  zx_status_t StartOngoingActivity(OngoingActivityId id, zx::time time);
  zx_status_t EndOngoingActivity(OngoingActivityId id, zx::time time);

 private:
  void ProcessEvent(const Event& event, zx::time time);
  void ProcessActivityStart(OngoingActivityId id);
  void ProcessActivityEnd(OngoingActivityId id);

  void StartTimer(zx::duration delay);
  void HandleTimeout();

  // Underlying state machine.
  ActivityStateMachine state_machine_;

  // The time of the most recent state transition.
  zx::time last_transition_time_;

  // Dispatcher to run operations on.
  async_dispatcher_t* dispatcher_;

  // A callback which is invoked whenever a state transition occurs.
  StateChangedCallback state_changed_callback_;

  // Set of ongoing activities. Activity IDs are added to this set by ProcessActivityStart() and
  // are removed by ProcessActivityEnd().
  //
  // While the map is non-empty, it is assumed that an activity is ongoing and thus no TIMEOUT
  // events will be delivered to the state machine.
  std::set<OngoingActivityId> ongoing_activities_;

  // A task which is posted on |dispatcher_| to trigger a timeout from a particular state.
  // The task is posted when a state with a timeout is entered.
  // The task is re-posted whenever an event is received.
  // The task is cancelled if a state which has no timeout is entered, or if an ongoing activity
  // starts.
  async::TaskClosureMethod<StateMachineDriver, &StateMachineDriver::HandleTimeout> timeout_task_{
      this};

  // Generates weak references to this object, which are appropriate to pass into asynchronous
  // callbacks that need to access this object. (The references are automatically invalidated
  // if this object is destroyed.)
  fxl::WeakPtrFactory<StateMachineDriver> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StateMachineDriver);
};

}  // namespace activity_service

#endif  // GARNET_BIN_UI_ACTIVITY_SERVICE_STATE_MACHINE_DRIVER_H_
