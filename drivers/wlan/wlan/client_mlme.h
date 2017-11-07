// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "mlme.h"

#include <ddk/protocol/wlan.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

namespace wlan {

class DeviceInterface;
struct MgmtFrameHeader;
class Packet;
class Scanner;
class Station;

// ClientMlme is a MLME which operates in non-AP mode. It is not thread-safe.
class ClientMlme : public Mlme {
   public:
    explicit ClientMlme(DeviceInterface* device);
    ~ClientMlme();

    // Mlme interface methods.
    zx_status_t Init() override;
    zx_status_t PreChannelChange(wlan_channel_t chan) override;
    zx_status_t PostChannelChange() override;
    zx_status_t HandleTimeout(const ObjectId id) override;

    zx_status_t HandleEthFrame(const BaseFrame<EthernetII>* frame) override;

    // Service Message handlers.

    zx_status_t HandleMlmeScanReq(ScanRequestPtr req) override;
    zx_status_t HandleMlmeJoinReq(JoinRequestPtr req) override;
    zx_status_t HandleMlmeAuthReq(AuthenticateRequestPtr req) override;
    zx_status_t HandleMlmeDeauthReq(DeauthenticateRequestPtr req) override;
    zx_status_t HandleMlmeAssocReq(AssociateRequestPtr req) override;
    zx_status_t HandleMlmeEapolReq(EapolRequestPtr req) override;
    zx_status_t HandleMlmeSetKeysReq(SetKeysRequestPtr req) override;

    // Data frame handlers.

    zx_status_t HandleNullDataFrame(const DataFrameHeader* frame,
                                    const wlan_rx_info_t* rxinfo) override;
    zx_status_t HandleDataFrame(const DataFrame<LlcHeader>* frame,
                                const wlan_rx_info_t* rxinfo) override;

    // Management frame handlers.

    zx_status_t HandleBeacon(const MgmtFrame<Beacon>* frame, const wlan_rx_info_t* rxinfo) override;
    zx_status_t HandleProbeResponse(const MgmtFrame<ProbeResponse>* frame,
                                    const wlan_rx_info_t* rxinfo) override;
    zx_status_t HandleAuthentication(const MgmtFrame<Authentication>* frame,
                                     const wlan_rx_info_t* rxinfo) override;
    zx_status_t HandleDeauthentication(const MgmtFrame<Deauthentication>* frame,
                                       const wlan_rx_info_t* rxinfo) override;
    zx_status_t HandleAssociationResponse(const MgmtFrame<AssociationResponse>* frame,
                                          const wlan_rx_info_t* rxinfo) override;
    zx_status_t HandleDisassociation(const MgmtFrame<Disassociation>* frame,
                                     const wlan_rx_info_t* rxinfo) override;
    zx_status_t HandleAddBaRequest(const MgmtFrame<AddBaRequestFrame>* frame,
                                   const wlan_rx_info_t* rxinfo) override;

    bool IsStaValid() const;

    DeviceInterface* const device_;

    fbl::unique_ptr<Scanner> scanner_;
    // TODO(tkilbourn): track other STAs
    fbl::unique_ptr<Station> sta_;
};

}  // namespace wlan
