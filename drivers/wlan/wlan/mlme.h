// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "mac_frame.h"

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

#include <ddk/protocol/wlan.h>
#include <zircon/types.h>

namespace wlan {

class DeviceInterface;
class Packet;
class ObjectId;

// Mlme is the Mac sub-Layer Management Entity for the wlan driver.
class Mlme {
   public:
    virtual ~Mlme() {}
    virtual zx_status_t Init() = 0;

    // Called before a channel change happens.
    virtual zx_status_t PreChannelChange(wlan_channel_t chan) = 0;
    // Called after a channel change is complete. The DeviceState channel will reflect the channel,
    // whether it changed or not.
    virtual zx_status_t PostChannelChange() = 0;
    virtual zx_status_t HandleTimeout(const ObjectId id) = 0;

    virtual zx_status_t HandleEthFrame(const BaseFrame<EthernetII>* frame) { return ZX_OK; }

    // Service Message handlers.

    virtual zx_status_t HandleMlmeScanReq(ScanRequestPtr req) { return ZX_OK; }

    virtual zx_status_t HandleMlmeJoinReq(JoinRequestPtr req) { return ZX_OK; }

    virtual zx_status_t HandleMlmeAuthReq(AuthenticateRequestPtr req) { return ZX_OK; }

    virtual zx_status_t HandleMlmeDeauthReq(DeauthenticateRequestPtr req) { return ZX_OK; }

    virtual zx_status_t HandleMlmeAssocReq(AssociateRequestPtr req) { return ZX_OK; }

    virtual zx_status_t HandleMlmeEapolReq(EapolRequestPtr req) { return ZX_OK; }

    virtual zx_status_t HandleMlmeSetKeysReq(SetKeysRequestPtr req) { return ZX_OK; }

    // Data frame handlers.

    virtual zx_status_t HandleNullDataFrame(const DataFrameHeader* frame,
                                            const wlan_rx_info_t* rxinfo) {
        return ZX_OK;
    }

    virtual zx_status_t HandleDataFrame(const DataFrame<LlcHeader>* frame,
                                        const wlan_rx_info_t* rxinfo) {
        return ZX_OK;
    }

    // Management frame handlers.

    virtual zx_status_t HandleBeacon(const MgmtFrame<Beacon>* frame, const wlan_rx_info_t* rxinfo) {
        return ZX_OK;
    }

    virtual zx_status_t HandleProbeResponse(const MgmtFrame<ProbeResponse>* frame,
                                            const wlan_rx_info_t* rxinfo) {
        return ZX_OK;
    }

    virtual zx_status_t HandleAuthentication(const MgmtFrame<Authentication>* frame,
                                             const wlan_rx_info_t* rxinfo) {
        return ZX_OK;
    }

    virtual zx_status_t HandleDeauthentication(const MgmtFrame<Deauthentication>* frame,
                                               const wlan_rx_info_t* rxinfo) {
        return ZX_OK;
    }

    virtual zx_status_t HandleAssociationResponse(const MgmtFrame<AssociationResponse>* frame,
                                                  const wlan_rx_info_t* rxinfo) {
        return ZX_OK;
    }

    virtual zx_status_t HandleDisassociation(const MgmtFrame<Disassociation>* frame,
                                             const wlan_rx_info_t* rxinfo) {
        return ZX_OK;
    }

    virtual zx_status_t HandleAddBaRequest(const MgmtFrame<AddBaRequestFrame>* frame,
                                           const wlan_rx_info_t* rxinfo) {
        return ZX_OK;
    }
};

}  // namespace wlan
