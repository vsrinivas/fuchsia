// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"

#include <lib/async/task.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <algorithm>

namespace wlan::simulation {

Environment::Environment()
    : signal_loss_model_(std::unique_ptr<SignalLossModel>(new LogSignalLossModel())) {
  // Construct the dispatcher ops for task post/cancel using the notification system.
  static const async_ops_t ops = {
      .v1 = {
          .now =
              [](async_dispatcher_t* dispatcher) {
                return static_cast<Dispatcher*>(dispatcher)->parent->GetTime().get();
              },
          .post_task =
              [](async_dispatcher_t* dispatcher, async_task_t* task) {
                return static_cast<Dispatcher*>(dispatcher)->parent->PostTask(task);
              },
          .cancel_task =
              [](async_dispatcher_t* dispatcher, async_task_t* task) {
                return static_cast<Dispatcher*>(dispatcher)->parent->CancelTask(task);
              },
      },
  };
  dispatcher_.parent = this;
  dispatcher_.ops = &ops;
}

Environment::~Environment() {
  while (!events_.empty()) {
    auto& event = events_.front();
    time_ = std::max(time_, event.deadline);
    event.fn(ZX_ERR_CANCELED);
    events_.pop_front();
  }
}

void Environment::Run(zx::duration run_time_limit) {
  std::lock_guard lock(event_mutex_);
  const zx::time run_deadline = time_ + run_time_limit;
  while (!events_.empty()) {
    auto& event = events_.front();
    if (event.deadline > run_deadline) {
      break;
    }

    std::function<void(zx_status_t)> fn = std::move(event.fn);
    time_ = std::max(time_, event.deadline);
    events_.pop_front();

    // Send event to client who requested it
    event_mutex_.unlock();
    fn(ZX_OK);
    event_mutex_.lock();
  }
}

void Environment::Tx(const SimFrame& frame, const WlanTxInfo& tx_info, StationIfc* sender) {
  std::shared_ptr<const SimFrame> tx_frame(frame.CopyFrame());
  for (auto& staLocPair : stations_) {
    StationIfc* sta = staLocPair.first;
    if (sta != sender) {
      double signal_strength =
          signal_loss_model_->CalcSignalStrength(&stations_.at(sender), &stations_.at(sta));
      auto rx_info = std::make_shared<WlanRxInfo>();
      rx_info->channel = tx_info.channel;
      rx_info->signal_strength = signal_strength;
      // Only deliver frame if the station is sensitive enough to pick it up
      if (signal_strength >= sta->rx_sensitivity_) {
        zx::duration trans_delay = CalcTransTime(sender, sta);
        ScheduleNotification(
            std::bind(&Environment::HandleTxNotification, this, sta, tx_frame, rx_info),
            trans_delay);
      }
    }
  }
}

zx::duration Environment::CalcTransTime(StationIfc* staTx, StationIfc* staRx) {
  Location location_tx = stations_.at(staTx);
  Location location_rx = stations_.at(staRx);
  double distance = location_tx.distanceFrom(&location_rx);

  // Calcualte how many nanoseconds are needed to do transmission.
  int64_t trans_time = distance / kRadioWaveVelocity;
  return zx::nsec(1) * trans_time;
}

zx_status_t Environment::ScheduleNotification(std::function<void()> fn, zx::duration delay,
                                              uint64_t* id_out) {
  // Disallow past events
  if (delay < zx::usec(0)) {
    return ZX_ERR_INVALID_ARGS;
  }
  const zx::time deadline = time_ + delay;

  uint64_t id = 0;
  {
    std::lock_guard lock(event_mutex_);
    id = event_id_++;
    // Our events are sorted in order of ascending deadline, then by order of insertion.
    const auto end_iter =
        std::find_if(events_.begin(), events_.end(),
                     [deadline](const EnvironmentEvent& e) { return e.deadline > deadline; });
    events_.emplace(
        end_iter,
        [fn = std::move(fn)](zx_status_t status) {
          // Notifications scheduled through ScheduleNotfication() do not get explicit cancellation
          // on shutdown of the environment.
          if (status == ZX_OK) {
            fn();
          }
        },
        deadline, id);
    latest_event_deadline_ = std::max(latest_event_deadline_, deadline);
  }

  if (id_out != nullptr) {
    *id_out = id;
  }

  return ZX_OK;
}

zx_status_t Environment::CancelNotification(uint64_t id) {
  std::lock_guard lock(event_mutex_);
  const auto remove_iter = std::remove_if(events_.begin(), events_.end(),
                                          [id](const EnvironmentEvent& e) { return e.id == id; });
  if (remove_iter == events_.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  events_.erase(remove_iter, events_.end());
  return ZX_OK;
}

zx::time Environment::GetTime() const {
  std::lock_guard lock(event_mutex_);
  return time_;
}

zx::time Environment::GetLatestEventTime() const {
  std::lock_guard lock(event_mutex_);
  return latest_event_deadline_;
}

zx_status_t Environment::PostTask(async_task_t* task) {
  uint64_t id = 0;
  {
    std::lock_guard lock(event_mutex_);
    id = event_id_++;
    const auto end_iter =
        std::find_if(events_.begin(), events_.end(),
                     [deadline = zx::time(task->deadline)](const EnvironmentEvent& e) {
                       return e.deadline > deadline;
                     });
    events_.emplace(
        end_iter,
        [dispatcher = &dispatcher_, task](zx_status_t status) {
          task->handler(dispatcher, task, status);
        },
        zx::time(task->deadline), id);
    latest_event_deadline_ = std::max(latest_event_deadline_, zx::time(task->deadline));
  }

  task->state.reserved[0] = id;
  return ZX_OK;
}

zx_status_t Environment::CancelTask(async_task_t* task) {
  return CancelNotification(task->state.reserved[0]);
}

async_dispatcher_t* Environment::GetDispatcher() { return &dispatcher_; }

void Environment::HandleTxNotification(StationIfc* sta, std::shared_ptr<const SimFrame> frame,
                                       std::shared_ptr<const WlanRxInfo> rx_info) {
  sta->Rx(frame, rx_info);
}

}  // namespace wlan::simulation
