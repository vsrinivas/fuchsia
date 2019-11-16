// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sim-env.h"

#include <zircon/assert.h>

namespace wlan::simulation {

void Environment::Run() {
  while (!events_.empty()) {
    auto event = std::move(events_.front());
    events_.pop_front();

    // Make sure that time is always moving forward
    ZX_ASSERT(event->time >= time_);
    time_ = event->time;

    // Send event to client who requested it
    event->requester->ReceiveNotification(event->payload);
  }
}

void Environment::TxBeacon(StationIfc* sender, const wlan_channel_t& channel,
                           const wlan_ssid_t& ssid, const common::MacAddr& bssid) {
  for (auto sta : stations_) {
    if (sta != sender) {
      sta->RxBeacon(channel, ssid, bssid);
    }
  }
}

void Environment::TxAssocReq(StationIfc* sender, const wlan_channel_t& channel,
                             const common::MacAddr& src, const common::MacAddr& bssid) {
  for (auto sta : stations_) {
    if (sta != sender) {
      sta->RxAssocReq(channel, src, bssid);
    }
  }
}

void Environment::TxAssocResp(StationIfc* sender, const wlan_channel_t& channel,
                              const common::MacAddr& src, const common::MacAddr& dst,
                              uint16_t status) {
  for (auto sta : stations_) {
    if (sta != sender) {
      sta->RxAssocResp(channel, src, dst, status);
    }
  }
}

void Environment::TxProbeReq(StationIfc* sender, const wlan_channel_t& channel,
                             const common::MacAddr& src) {
  for (auto sta : stations_) {
    if (sta != sender) {
      sta->RxProbeReq(channel, src);
    }
  }
}

void Environment::TxProbeResp(StationIfc* sender, const wlan_channel_t& channel,
                              const common::MacAddr& src, const common::MacAddr& dst,
                              const wlan_ssid_t& ssid) {
  for (auto sta : stations_) {
    if (sta != sender) {
      sta->RxProbeResp(channel, src, dst, ssid);
    }
  }
}

zx_status_t Environment::ScheduleNotification(StationIfc* sta, zx::duration delay, void* payload) {
  // Disallow past events
  if (delay < zx::usec(0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto event = std::make_unique<EnvironmentEvent>();
  event->time = time_ + delay;
  event->requester = sta;
  event->payload = payload;

  // Keep our events sorted in ascending order of absolute time. When multiple events are
  // scheduled for the same time, the first requested will be processed first.
  auto event_iter = events_.begin();
  while (event_iter != events_.end() && (*event_iter)->time <= event->time) {
    event_iter++;
  }
  events_.insert(event_iter, std::move(event));

  return ZX_OK;
}

}  // namespace wlan::simulation
