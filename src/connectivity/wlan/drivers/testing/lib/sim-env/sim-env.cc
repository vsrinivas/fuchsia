// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sim-env.h"

#include <zircon/assert.h>

namespace wlan::simulation {

uint64_t Environment::event_count_ = 0;

Environment::Environment()
    : signal_loss_model_(std::unique_ptr<SignalLossModel>(new LogSignalLossModel())){};

void Environment::Run(std::optional<zx::duration> run_time_limit) {
  // If run_time_limit is indicated, run the event queue for this time limit from CURRENT TIME
  // POINT.
  if (run_time_limit) {
    auto fn = std::make_unique<std::function<void()>>();
    *fn = std::bind(&Environment::StopRunning, this);
    ScheduleNotification(std::move(fn), run_time_limit.value());
  }

  while (!stop_sign_ && !events_.empty()) {
    auto event = std::move(events_.front());
    events_.pop_front();
    // Make sure that time is always moving forward
    ZX_ASSERT(event->time >= time_);
    // Increase current running time for this run.
    time_ = event->time;
    // Send event to client who requested it
    (*(event->fn))();
  }
  stop_sign_ = false;
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
        auto tx_handler = std::make_unique<std::function<void()>>();
        *tx_handler = std::bind(&Environment::HandleTxNotification, this, sta, tx_frame, rx_info);
        ScheduleNotification(std::move(tx_handler), trans_delay);
      }
    }
  }
}

zx_status_t Environment::ScheduleNotification(std::unique_ptr<std::function<void()>> fn,
                                              zx::duration delay, uint64_t* id_out) {
  uint64_t id = event_count_++;
  // Disallow past events
  if (delay < zx::usec(0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto event = std::make_unique<EnvironmentEvent>();
  event->id = id;
  event->time = time_ + delay;
  event->fn = std::move(fn);

  // Keep our events sorted in ascending order of absolute time. When multiple events are
  // scheduled for the same time, the first requested will be processed first.
  auto event_iter = events_.begin();
  while (event_iter != events_.end() && (*event_iter)->time <= event->time) {
    event_iter++;
  }
  events_.insert(event_iter, std::move(event));

  if (id_out != nullptr) {
    *id_out = id;
  }

  return ZX_OK;
}

// Since all events are processed synchronously, we don't have to worry about locking.
zx_status_t Environment::CancelNotification(uint64_t id) {
  for (auto& event_iter : events_) {
    if (event_iter->id == id) {
      events_.remove(event_iter);
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

void Environment::HandleTxNotification(StationIfc* sta, std::shared_ptr<const SimFrame> frame,
                                       std::shared_ptr<const WlanRxInfo> rx_info) {
  sta->Rx(frame, rx_info);
}

zx::duration Environment::CalcTransTime(StationIfc* staTx, StationIfc* staRx) {
  Location location_tx = stations_.at(staTx);
  Location location_rx = stations_.at(staRx);
  double distance = location_tx.distanceFrom(&location_rx);

  // Calcualte how many nanoseconds are needed to do transmission.
  int64_t trans_time = distance / kRadioWaveVelocity;
  return zx::nsec(1) * trans_time;
}

void Environment::StopRunning() { stop_sign_ = true; }

}  // namespace wlan::simulation
