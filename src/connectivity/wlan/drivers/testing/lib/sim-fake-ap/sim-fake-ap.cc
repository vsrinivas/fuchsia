// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sim-fake-ap.h"

#include <zircon/assert.h>

#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::simulation {

bool FakeAp::CanReceiveChannel(const wlan_channel_t& channel) {
  // For now, require an exact match
  return ((channel.primary == chan_.primary) && (channel.cbw == chan_.cbw) &&
          (channel.secondary80 == chan_.secondary80));
}

void FakeAp::ScheduleNextBeacon() {
  auto beacon_handler = new std::function<void()>;
  *beacon_handler = std::bind(&FakeAp::HandleBeaconNotification, this);
  environment_->ScheduleNotification(this, beacon_state_.beacon_interval,
                                     static_cast<void*>(beacon_handler),
                                     &beacon_state_.beacon_notification_id);
}

void FakeAp::EnableBeacon(zx::duration beacon_period) {
  if (beacon_state_.is_beaconing) {
    // If we're already beaconing, we want to cancel any pending scheduled beacons before
    // restarting with the new beacon period.
    DisableBeacon();
  }

  // First beacon is sent out immediately
  SimBeaconFrame beacon_frame(this, ssid_, bssid_);
  environment_->Tx(&beacon_frame, chan_);

  beacon_state_.is_beaconing = true;
  beacon_state_.beacon_interval = beacon_period;

  ScheduleNextBeacon();
}

void FakeAp::DisableBeacon() {
  beacon_state_.is_beaconing = false;
  ZX_ASSERT(environment_->CancelNotification(this, beacon_state_.beacon_notification_id) == ZX_OK);
}

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

void FakeAp::Rx(const SimFrame* frame, const wlan_channel_t& channel) {
  // Make sure we heard it
  if (!CanReceiveChannel(channel)) {
    return;
  }

  switch (frame->FrameType()) {
    case SimFrame::FRAME_TYPE_MGMT: {
      auto mgmt_frame = static_cast<const SimManagementFrame*>(frame);
      RxMgmtFrame(mgmt_frame);
      break;
    }

    default:
      break;
  }
}

void FakeAp::RxMgmtFrame(const SimManagementFrame* mgmt_frame) {
  switch (mgmt_frame->MgmtFrameType()) {
    case SimManagementFrame::FRAME_TYPE_PROBE_REQ: {
      auto probe_req_frame = static_cast<const SimProbeReqFrame*>(mgmt_frame);
      ScheduleProbeResp(probe_req_frame->src_addr_);
      break;
    }

    case SimManagementFrame::FRAME_TYPE_ASSOC_REQ: {
      auto assoc_req_frame = static_cast<const SimAssocReqFrame*>(mgmt_frame);

      // Ignore requests that are not for us
      if (assoc_req_frame->bssid_ != bssid_) {
        return;
      }

      if (assoc_handling_mode_ == ASSOC_IGNORED) {
        return;
      }

      if (assoc_handling_mode_ == ASSOC_REJECTED) {
        ScheduleAssocResp(WLAN_STATUS_CODE_REFUSED, assoc_req_frame->src_addr_);
        return;
      }

      // Make sure the client is not already associated
      for (auto client : clients_) {
        if (client == assoc_req_frame->src_addr_) {
          // Client is already associated
          ScheduleAssocResp(WLAN_STATUS_CODE_REFUSED_TEMPORARILY, assoc_req_frame->src_addr_);
          return;
        }
      }

      clients_.push_back(assoc_req_frame->src_addr_);
      ScheduleAssocResp(WLAN_STATUS_CODE_SUCCESS, assoc_req_frame->src_addr_);
      break;
    }

    case SimManagementFrame::FRAME_TYPE_DISASSOC_REQ: {
      auto disassoc_req_frame = static_cast<const SimDisassocReqFrame*>(mgmt_frame);
      // Ignore requests that are not for us
      if (disassoc_req_frame->dst_addr_ != bssid_) {
        return;
      }

      // Make sure the client is already associated
      for (auto client : clients_) {
        if (client == disassoc_req_frame->src_addr_) {
          // Client is already associated
          clients_.remove(disassoc_req_frame->src_addr_);
          return;
        }
      }
      break;
    }

    default:
      break;
  }
}

zx_status_t FakeAp::DisassocSta(const common::MacAddr& sta_mac, uint16_t reason) {
  // Make sure the client is already associated
  SimDisassocReqFrame disassoc_req_frame(this, bssid_, sta_mac, reason);
  for (auto client : clients_) {
    if (client == sta_mac) {
      // Client is already associated
      environment_->Tx(&disassoc_req_frame, chan_);
      clients_.remove(sta_mac);
      return ZX_OK;
    }
  }
  // client not found
  return ZX_ERR_INVALID_ARGS;
}

void FakeAp::HandleBeaconNotification() {
  ZX_ASSERT(beacon_state_.is_beaconing);
  SimBeaconFrame beacon_frame(this, ssid_, bssid_);
  environment_->Tx(&beacon_frame, chan_);
  ScheduleNextBeacon();
}

void FakeAp::HandleAssocRespNotification(uint16_t status, common::MacAddr dst) {
  SimAssocRespFrame assoc_resp_frame(this, bssid_, dst, status);
  environment_->Tx(&assoc_resp_frame, chan_);
}

void FakeAp::HandleProbeRespNotification(common::MacAddr dst) {
  SimProbeRespFrame probe_resp_frame(this, bssid_, dst, ssid_);
  environment_->Tx(&probe_resp_frame, chan_);
}

void FakeAp::ReceiveNotification(void* payload) {
  auto handler = static_cast<std::function<void()>*>(payload);
  (*handler)();
  delete handler;
}

void FakeAp::SetAssocHandling(enum AssocHandling mode) { assoc_handling_mode_ = mode; }

}  // namespace wlan::simulation
