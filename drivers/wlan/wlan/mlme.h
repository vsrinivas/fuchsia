// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/wlan.h>
#include <zircon/types.h>

namespace wlan {

class DeviceInterface;
class Packet;

// Mlme is the Mac sub-Layer Management Entity for the wlan driver.
class Mlme {
   public:
    virtual zx_status_t Init() = 0;

    // Called before a channel change happens.
    virtual zx_status_t PreChannelChange(wlan_channel_t chan) = 0;
    // Called after a channel change is complete. The DeviceState channel will reflect the channel,
    // whether it changed or not.
    virtual zx_status_t PostChannelChange() = 0;

    // MAC frame handlers
    virtual zx_status_t HandleCtrlPacket(const Packet* packet) = 0;
    virtual zx_status_t HandleDataPacket(const Packet* packet) = 0;
    virtual zx_status_t HandleMgmtPacket(const Packet* packet) = 0;
    virtual zx_status_t HandleEthPacket(const Packet* packet) = 0;
    virtual zx_status_t HandleSvcPacket(const Packet* packet) = 0;
};

}  // namespace wlan
