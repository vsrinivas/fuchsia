// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <apps/wlan/services/wlan_mlme.fidl-common.h>
#include <ddk/protocol/wlan.h>
#include <magenta/types.h>
#include <mxtl/unique_ptr.h>

namespace wlan {

class DeviceInterface;
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

    const uint8_t* bssid() const;
    wlan_channel_t channel() const;

    mx_status_t Join(JoinRequestPtr req);
    mx_status_t HandleBeacon(const Packet* packet);
    mx_status_t HandleTimeout();

    const Timer& timer() const { return *timer_; }

  private:
    mx_status_t SendJoinResponse();

    DeviceInterface* device_;
    mxtl::unique_ptr<Timer> timer_;
    BSSDescriptionPtr bss_;

    WlanState state_ = WlanState::kUnjoined;
    mx_time_t join_timeout_ = 0;
    mx_time_t last_seen_ = 0;
};

}  // namespace wlan
