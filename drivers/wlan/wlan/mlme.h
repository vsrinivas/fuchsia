// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "frame_handler.h"
#include "mac_frame.h"

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

#include <ddk/protocol/wlan.h>
#include <zircon/types.h>

namespace wlan {

class DeviceInterface;
class Packet;
class ObjectId;

// Mlme is the Mac sub-Layer Management Entity for the wlan driver.
class Mlme : public FrameHandler {
   public:
    virtual ~Mlme() {}
    virtual zx_status_t Init() = 0;

    // Called before a channel change happens.
    virtual zx_status_t PreChannelChange(wlan_channel_t chan) = 0;
    // Called after a channel change is complete. The DeviceState channel will reflect the channel,
    // whether it changed or not.
    virtual zx_status_t PostChannelChange() = 0;
    virtual zx_status_t HandleTimeout(const ObjectId id) = 0;
};

}  // namespace wlan
