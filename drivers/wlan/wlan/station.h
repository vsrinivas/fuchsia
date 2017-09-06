// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device_interface.h"
#include "mac_frame.h"

#include <apps/wlan/services/wlan_mlme.fidl-common.h>
#include <apps/wlan/services/wlan_mlme_ext.fidl-common.h>
#include <ddk/protocol/wlan.h>
#include <drivers/wifi/common/moving_average.h>
#include <magenta/types.h>
#include <fbl/unique_ptr.h>

namespace wlan {

class Packet;
class Timer;

class Station {
  public:
    Station(DeviceInterface* device, fbl::unique_ptr<Timer> timer);

    enum class PortState : bool {
      kBlocked = false,
      kOpen = true
    };

    enum class WlanState {
        // State 0
        kUnjoined,

        // State 1
        kUnauthenticated,

        // State 2
        kAuthenticated,
        // State 3/4
        // TODO(tkilbourn): distinguish between states where 802.1X ports are blocked
        kAssociated,
    };

    void Reset();

    const DeviceAddress* bssid() const {
        if (bss_.is_null()) {
            return nullptr;
        }
        return &address_;
    }

    uint16_t aid() const { return aid_; }

    wlan_channel_t channel() const {
        MX_DEBUG_ASSERT(state_ != WlanState::kUnjoined);
        MX_DEBUG_ASSERT(!bss_.is_null());
        return wlan_channel_t{ bss_->channel };
    }

    mx_status_t Join(JoinRequestPtr req);
    mx_status_t Authenticate(AuthenticateRequestPtr req);
    mx_status_t Associate(AssociateRequestPtr req);

    mx_status_t HandleBeacon(const Packet* packet);
    mx_status_t HandleAuthentication(const Packet* packet);
    mx_status_t HandleDeauthentication(const Packet* packet);
    mx_status_t HandleAssociationResponse(const Packet* packet);
    mx_status_t HandleDisassociation(const Packet* packet);
    mx_status_t HandleData(const Packet* packet);
    mx_status_t HandleEth(const Packet* packet);
    mx_status_t HandleTimeout();

    mx_status_t PreChannelChange(wlan_channel_t chan);
    mx_status_t PostChannelChange();

    const Timer& timer() const { return *timer_; }


  private:
    mx_status_t SendJoinResponse();
    mx_status_t SendAuthResponse(AuthenticateResultCodes code);
    mx_status_t SendDeauthIndication(uint16_t code);
    mx_status_t SendAssocResponse(AssociateResultCodes code);
    mx_status_t SendDisassociateIndication(uint16_t code);

    mx_status_t SendSignalReportIndication(uint8_t rssi);
    mx_status_t SendEapolIndication(const EapolFrame* eapol, const uint8_t src[],
                                    const uint8_t dst[]);

    mx_status_t SetPowerManagementMode(bool ps_mode);
    mx_status_t SendPsPoll();

    mx_time_t deadline_after_bcn_period(mx_duration_t tus);
    uint16_t next_seq();

    DeviceInterface* device_;
    fbl::unique_ptr<Timer> timer_;
    BSSDescriptionPtr bss_;
    DeviceAddress address_;
    uint16_t last_seq_ = kMaxSequenceNumber;

    WlanState state_ = WlanState::kUnjoined;
    mx_time_t join_timeout_ = 0;
    mx_time_t auth_timeout_ = 0;
    mx_time_t assoc_timeout_ = 0;
    mx_time_t signal_report_timeout_ = 0;
    mx_time_t last_seen_ = 0;
    uint16_t aid_ = 0;
    common::MovingAverage<uint8_t, uint16_t, 20> avg_rssi_;
    AuthAlgorithm auth_alg_ = AuthAlgorithm::kOpenSystem;
    PortState controlled_port_ = PortState::kBlocked;
};

}  // namespace wlan
