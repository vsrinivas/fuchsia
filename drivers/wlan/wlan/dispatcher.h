// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/wlan.h>
#include <fbl/unique_ptr.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/mlme.h>
#include <zircon/types.h>

namespace wlan {

// The Dispatcher converts Packets, forwarded by the Device, into concrete frames, such as
// management frames, or service messages.

class Dispatcher {
   public:
    explicit Dispatcher(DeviceInterface* device);
    ~Dispatcher();

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
    template <typename Message, typename FidlStruct = ::fidl::StructPtr<Message>>
    zx_status_t HandleMlmeMethod(const Packet* packet, Method method);
    template <typename Message>
    zx_status_t HandleMlmeMethodInlinedStruct(const Packet* packet, Method method) {
        return HandleMlmeMethod<Message, ::fidl::InlinedStructPtr<Message>>(packet, method);
    }
    zx_status_t HandleActionPacket(const Packet* packet, const MgmtFrameHeader* hdr,
                                   const ActionFrame* action, const wlan_rx_info_t* rxinfo);

    DeviceInterface* device_;
    // Created and destroyed dynamically:
    // - Creates ClientMlme when MLME-JOIN.request or MLME-SCAN.request was received.
    // - Creates ApMlme when MLME-START.request was received.
    // - Destroys Mlme when MLME-RESET.request was received.
    // Note: Mode can only be changed at boot up or when MLME-RESET.request was sent in between mode
    // changes.
    fbl::unique_ptr<Mlme> mlme_;
};

}  // namespace wlan
