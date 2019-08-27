// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sim-fake-ap.h"

namespace wlan::simulation {

void FakeAp::ScheduleNextBeacon() {
  Notification* notification = new Notification;
  notification->type = Notification::BEACON;
  notification->beacon_info.beacon_id = beacon_index_;
  environment_->ScheduleNotification(this, beacon_interval_, static_cast<void*>(notification));
}

void FakeAp::EnableBeacon(zx::duration beacon_period) {
  // First beacon is sent out immediately
  environment_->TxBeacon(this, chan_, ssid_, bssid_);

  is_beaconing_ = true;
  beacon_interval_ = beacon_period;
  beacon_index_++;

  ScheduleNextBeacon();
}

void FakeAp::DisableBeacon() { is_beaconing_ = false; }

void FakeAp::HandleBeaconNotification(const BeaconInfo& info) {
  // Check the beacon index to verify that this was not an event that was scheduled for another
  // beacon.
  if (!is_beaconing_ || (info.beacon_id != beacon_index_)) {
    return;
  }

  environment_->TxBeacon(this, chan_, ssid_, bssid_);

  ScheduleNextBeacon();
}

void FakeAp::ReceiveNotification(void* payload) {
  Notification* notification = static_cast<Notification*>(payload);

  if (notification->type == Notification::BEACON) {
    HandleBeaconNotification(notification->beacon_info);
  }

  delete notification;
}

}  // namespace wlan::simulation
