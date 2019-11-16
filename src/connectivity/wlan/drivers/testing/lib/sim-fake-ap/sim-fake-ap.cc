// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sim-fake-ap.h"

#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::simulation {

bool FakeAp::CanReceiveChannel(const wlan_channel_t& channel) {
  // For now, require an exact match
  return ((channel.primary == chan_.primary) && (channel.cbw == chan_.cbw) &&
          (channel.secondary80 == chan_.secondary80));
}

void FakeAp::ScheduleNextBeacon() {
  auto beacon_handler = new std::function<void()>;
  *beacon_handler = std::bind(&FakeAp::HandleBeaconNotification, this, beacon_index_);
  environment_->ScheduleNotification(this, beacon_interval_, static_cast<void*>(beacon_handler));
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

void FakeAp::ScheduleAssocResp(uint16_t status, const common::MacAddr& dst) {
  auto handler = new std::function<void()>;
  *handler = std::bind(&FakeAp::HandleAssocRespNotification, this, status, dst);
  environment_->ScheduleNotification(this, assoc_resp_interval_, static_cast<void*>(handler));
}

void FakeAp::ScheduleProbeResp(const common::MacAddr& dst) {
  auto handler = new std::function<void()>;
  *handler = std::bind(&FakeAp::HandleProbeRespNotification, this, dst);
  environment_->ScheduleNotification(this, probe_resp_interval_, static_cast<void*>(handler));
}

void FakeAp::RxAssocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                        const common::MacAddr& bssid) {
  // Make sure we heard the message
  if (!CanReceiveChannel(channel)) {
    return;
  }

  // Ignore requests that are not for us
  if (bssid != bssid_) {
    return;
  }

  // Make sure the client is not already associated
  for (auto client : clients_) {
    if (client == src) {
      // Client is already associated
      ScheduleAssocResp(WLAN_STATUS_CODE_REFUSED_TEMPORARILY, src);
      return;
    }
  }

  clients_.push_back(src);
  ScheduleAssocResp(WLAN_STATUS_CODE_SUCCESS, src);
}

void FakeAp::RxProbeReq(const wlan_channel_t& channel, const common::MacAddr& src) {
  // Make sure we heard the message
  if (!CanReceiveChannel(channel)) {
    return;
  }

  ScheduleProbeResp(src);
}

void FakeAp::HandleBeaconNotification(uint64_t beacon_id) {
  // Check the beacon index to verify that this was not an event that was scheduled for another
  // beacon.
  if (!is_beaconing_ || (beacon_id != beacon_index_)) {
    return;
  }

  environment_->TxBeacon(this, chan_, ssid_, bssid_);

  ScheduleNextBeacon();
}

void FakeAp::HandleAssocRespNotification(uint16_t status, common::MacAddr dst) {
  environment_->TxAssocResp(this, chan_, bssid_, dst, status);
}

void FakeAp::HandleProbeRespNotification(common::MacAddr dst) {
  environment_->TxProbeResp(this, chan_, bssid_, dst, ssid_);
}

void FakeAp::ReceiveNotification(void* payload) {
  auto handler = static_cast<std::function<void()>*>(payload);
  (*handler)();
  delete handler;
}

}  // namespace wlan::simulation
