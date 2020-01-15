// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ACTIVITY_STATE_MACHINE_DRIVER_H_
#define SRC_UI_BIN_ACTIVITY_STATE_MACHINE_DRIVER_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <set>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/bin/activity/activity_state_machine.h"
#include "src/ui/bin/activity/common.h"

namespace activity {

using VoidCallback = fit::function<void()>;

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

  fuchsia::ui::activity::State GetState() const;

  const ActivityStateMachine& state_machine() const { return state_machine_; };

  zx_status_t RegisterObserver(ObserverId id, StateChangedCallback callback);
  zx_status_t UnregisterObserver(ObserverId id);
  // Exposed for testing
  size_t num_observers() const { return observers_.size(); }

  // Inputs to the state machine. These methods enqueue a work item onto the driver's async loop to
  // handle the given activity, scheduling the work item to run at |time|.
  // If |time| was before the last state transition, it is ignored and ZX_ERR_OUT_OF_BOUNDS is
  // returned. (Events may be interpreted differently depending on the current state.)
  //
  // |callback| is invoked once the work item on the async loop is executed. If an error is
  // returned, |callback| is invoked immediately and synchronously.
  zx_status_t ReceiveDiscreteActivity(const fuchsia::ui::activity::DiscreteActivity& activity,
                                      zx::time time, VoidCallback callback);
  zx_status_t StartOngoingActivity(OngoingActivityId id, zx::time time, VoidCallback callback);
  zx_status_t EndOngoingActivity(OngoingActivityId id, zx::time time, VoidCallback callback);

  // Force the state machine into |state|.
  //
  // The state machine will continue to receive and process input, but observers will only be
  // notified of |state| and any future states set through this method.
  //
  // Passing std::nullopt will disable the override, which has the following effects:
  //  - Immediately notifies all listeners of the actual state of the state machine
  //  - Returns the state machine to its original behavior, where observers are notified of
  //    state transitions occuring due to received inputs.
  void SetOverrideState(std::optional<fuchsia::ui::activity::State> state);

 private:
  void ProcessEvent(const Event& event, zx::time time);
  void ProcessActivityStart(OngoingActivityId id);
  void ProcessActivityEnd(OngoingActivityId id);

  void StartTimer(zx::duration delay);
  void HandleTimeout();

  void NotifyObservers(fuchsia::ui::activity::State state, zx::time time) const;

  // An optional state override. When set, the state from state_machine_ continues to be updated,
  // but changes to that state are not sent to observers. See SetOverrideState() for details.
  std::optional<fuchsia::ui::activity::State> override_state_;

  // Underlying state machine.
  ActivityStateMachine state_machine_;

  // The time of the most recent state transition.
  zx::time last_transition_time_;

  // Dispatcher to run operations on.
  async_dispatcher_t* dispatcher_;

  // Observers to be notified whenever a state transition occurs.
  std::map<ObserverId, StateChangedCallback> observers_;

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

}  // namespace activity

#endif  // SRC_UI_BIN_ACTIVITY_STATE_MACHINE_DRIVER_H_
