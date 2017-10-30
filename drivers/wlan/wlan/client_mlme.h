// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/wlan.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

namespace wlan {

class DeviceInterface;
struct MgmtFrameHeader;
class Packet;
class Scanner;
class Station;

// Mlme is the Mac sub-Layer Management Entity for the wlan driver. It is not thread-safe.
class Mlme {
   public:
    explicit Mlme(DeviceInterface* device);
    ~Mlme();

    zx_status_t Init();

    zx_status_t HandlePacket(const Packet* packet);
    zx_status_t HandlePortPacket(uint64_t key);

    // Called before a channel change happens.
    zx_status_t PreChannelChange(wlan_channel_t chan);
    // Called after a channel change is complete. The DeviceState channel will reflect the channel,
    // whether it changed or not.
    zx_status_t PostChannelChange();

   private:
    // MAC frame handlers
    zx_status_t HandleCtrlPacket(const Packet* packet);
    zx_status_t HandleDataPacket(const Packet* packet);
    zx_status_t HandleMgmtPacket(const Packet* packet);
    zx_status_t HandleEthPacket(const Packet* packet);
    zx_status_t HandleSvcPacket(const Packet* packet);

    // Management frame handlers
    zx_status_t HandleBeacon(const Packet* packet);
    zx_status_t HandleProbeResponse(const Packet* packet);
    zx_status_t HandleAuthentication(const Packet* packet);
    zx_status_t HandleDeauthentication(const Packet* packet);
    zx_status_t HandleAssociationResponse(const Packet* packet);
    zx_status_t HandleDisassociation(const Packet* packet);
    zx_status_t HandleAction(const Packet* packet);

    bool IsStaValid() const;

    DeviceInterface* const device_;

    fbl::unique_ptr<Scanner> scanner_;
    // TODO(tkilbourn): track other STAs
    fbl::unique_ptr<Station> sta_;
};

}  // namespace wlan
