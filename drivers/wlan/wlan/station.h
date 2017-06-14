// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device_interface.h"
#include "mac_frame.h"

#include <apps/wlan/services/wlan_mlme.fidl-common.h>
#include <ddk/protocol/wlan.h>
#include <magenta/types.h>
#include <mxtl/unique_ptr.h>

namespace wlan {

class Packet;
class Timer;

class Station {
  public:
    Station(DeviceInterface* device, mxtl::unique_ptr<Timer> timer);

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
    mx_status_t HandleAssociationResponse(const Packet* packet);
    mx_status_t HandleTimeout();

    const Timer& timer() const { return *timer_; }


  private:
    mx_status_t SendJoinResponse();
    mx_status_t SendAuthResponse(AuthenticateResultCodes code);
    mx_status_t SendAssocResponse(AssociateResultCodes code);

    uint16_t next_seq();

    DeviceInterface* device_;
    mxtl::unique_ptr<Timer> timer_;
    BSSDescriptionPtr bss_;
    DeviceAddress address_;
    uint16_t last_seq_ = kMaxSequenceNumber;

    WlanState state_ = WlanState::kUnjoined;
    mx_time_t join_timeout_ = 0;
    mx_time_t auth_timeout_ = 0;
    mx_time_t assoc_timeout_ = 0;
    mx_time_t last_seen_ = 0;
    uint16_t aid_ = 0;

    AuthAlgorithm auth_alg_ = AuthAlgorithm::kOpenSystem;
};

}  // namespace wlan
