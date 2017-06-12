// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/wlan.h>
#include <magenta/types.h>
#include <mxtl/unique_ptr.h>

namespace wlan {

class DeviceInterface;
class Packet;
class Scanner;
class Station;

// Mlme is the Mac sub-Layer Management Entity for the wlan driver. It is not thread-safe.
class Mlme {
  public:
    explicit Mlme(DeviceInterface* device);
    ~Mlme();

    mx_status_t Init();

    mx_status_t HandlePacket(const Packet* packet);
    mx_status_t HandlePortPacket(uint64_t key);

    // Called before and after a channel change happens
    mx_status_t PreChannelChange(wlan_channel_t chan);
    mx_status_t PostChannelChange(wlan_channel_t chan);

  private:
    // MAC frame handlers
    mx_status_t HandleCtrlPacket(const Packet* packet);
    mx_status_t HandleDataPacket(const Packet* packet);
    mx_status_t HandleMgmtPacket(const Packet* packet);
    mx_status_t HandleSvcPacket(const Packet* packet);

    // Management frame handlers
    mx_status_t HandleBeacon(const Packet* packet);
    mx_status_t HandleProbeResponse(const Packet* packet);

    DeviceInterface* const device_;

    mxtl::unique_ptr<Scanner> scanner_;
    // TODO(tkilbourn): track other STAs
    mxtl::unique_ptr<Station> sta_;
};

}  // namespace wlan
