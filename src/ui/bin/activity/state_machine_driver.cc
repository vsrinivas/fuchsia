// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/activity/state_machine_driver.h"

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <map>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/bin/activity/activity_state_machine.h"
#include "src/ui/bin/activity/common.h"

namespace activity {

fuchsia::ui::activity::State StateMachineDriver::GetState() const {
  return override_state_ ? *override_state_ : state_machine_.state();
}

zx_status_t StateMachineDriver::RegisterObserver(ObserverId id, StateChangedCallback callback) {
  observers_.emplace(id, std::move(callback));
  return ZX_OK;
}

zx_status_t StateMachineDriver::UnregisterObserver(ObserverId id) {
  auto entry = observers_.find(id);
  if (entry == observers_.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  observers_.erase(entry);
  return ZX_OK;
}

zx_status_t StateMachineDriver::ReceiveDiscreteActivity(
    const fuchsia::ui::activity::DiscreteActivity& activity, zx::time time, VoidCallback callback) {
  if (time < last_transition_time_) {
    callback();
    return ZX_ERR_OUT_OF_RANGE;
  }
  auto event = ActivityStateMachine::EventForDiscreteActivity(activity);
  return async::PostTaskForTime(
      dispatcher_,
      [weak = weak_factory_.GetWeakPtr(), event, time, callback = std::move(callback)] {
        if (weak) {
          weak->ProcessEvent(event, time);
        }
        callback();
      },
      time);
}

zx_status_t StateMachineDriver::StartOngoingActivity(OngoingActivityId id, zx::time time,
                                                     VoidCallback callback) {
  if (time < last_transition_time_) {
    callback();
    return ZX_ERR_OUT_OF_RANGE;
  }
  return async::PostTaskForTime(
      dispatcher_,
      [weak = weak_factory_.GetWeakPtr(), id, time, callback = std::move(callback)] {
        if (weak) {
          auto event = ActivityStateMachine::EventForOngoingActivityStart();
          weak->ProcessActivityStart(id);
          weak->ProcessEvent(event, time);
        }
        callback();
      },
      time);
}

zx_status_t StateMachineDriver::EndOngoingActivity(OngoingActivityId id, zx::time time,
                                                   VoidCallback callback) {
  if (time < last_transition_time_) {
    callback();
    return ZX_ERR_OUT_OF_RANGE;
  }
  return async::PostTaskForTime(
      dispatcher_,
      [weak = weak_factory_.GetWeakPtr(), id, time, callback = std::move(callback)] {
        if (weak) {
          auto event = ActivityStateMachine::EventForOngoingActivityEnd();
          weak->ProcessActivityEnd(id);
          weak->ProcessEvent(event, time);
        }
        callback();
      },
      time);
}

void StateMachineDriver::SetOverrideState(std::optional<fuchsia::ui::activity::State> state) {
  bool should_notify = (state.has_value() != override_state_.has_value()) ||
                       (override_state_ && (state != override_state_));
  override_state_ = state;
  if (should_notify) {
    FXL_LOG(INFO) << "activity-service: entering state '" << GetState() << "'";
    NotifyObservers(GetState(), async::Now(dispatcher_));
  }
}

void StateMachineDriver::ProcessEvent(const Event& event, zx::time time) {
  auto state = state_machine_.state();
  state_machine_.ReceiveEvent(event);
  auto new_state = state_machine_.state();

  if (state != new_state) {
    last_transition_time_ = time;
    if (!override_state_) {
      FXL_LOG(INFO) << "activity-service: '" << state << "' -> '" << new_state << "' due to '"
                    << event << "'";
      NotifyObservers(new_state, time);
    }
  }

  timeout_task_.Cancel();
  auto timeout = ActivityStateMachine::TimeoutFor(new_state);
  if (ongoing_activities_.empty() && timeout) {
    StartTimer(*timeout);
  }
}

void StateMachineDriver::ProcessActivityStart(OngoingActivityId id) {
  if (ongoing_activities_.find(id) != ongoing_activities_.end()) {
    FXL_LOG(WARNING) << "Activity '" << id << "' already started, ignoring";
    return;
  }
  ongoing_activities_.insert(id);
  if (timeout_task_.is_pending()) {
    timeout_task_.Cancel();
  }
}

void StateMachineDriver::ProcessActivityEnd(OngoingActivityId id) {
  auto iter = ongoing_activities_.find(id);
  if (iter == ongoing_activities_.end()) {
    FXL_LOG(WARNING) << "Activity '" << id << "' spuriously ended, ignoring";
    return;
  }
  ongoing_activities_.erase(iter);
  if (!timeout_task_.is_pending()) {
    auto timeout = ActivityStateMachine::TimeoutFor(state_machine_.state());
    if (timeout) {
      StartTimer(*timeout);
    }
  }
}

void StateMachineDriver::StartTimer(zx::duration delay) {
  if (timeout_task_.is_pending() &&
      async::Now(dispatcher_) + delay < timeout_task_.last_deadline()) {
    return;
  }
  timeout_task_.Cancel();
  timeout_task_.PostDelayed(dispatcher_, delay);
}

void StateMachineDriver::HandleTimeout() {
  auto status = async::PostTask(dispatcher_, [weak = weak_factory_.GetWeakPtr()]() {
    if (weak) {
      weak->ProcessEvent(Event::TIMEOUT, async::Now(weak->dispatcher_));
    }
  });
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "activity-service: Failed to queue timeout event: "
                   << zx_status_get_string(status);
  }
}

void StateMachineDriver::NotifyObservers(fuchsia::ui::activity::State state, zx::time time) const {
  for (const auto& observer : observers_) {
    observer.second(state, time);
  }
}

}  // namespace activity
