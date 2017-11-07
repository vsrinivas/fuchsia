// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client_mlme.h"
#include "dispatcher.h"

#include "interface.h"
#include "logging.h"
#include "mac_frame.h"
#include "packet.h"
#include "scanner.h"
#include "serialize.h"
#include "station.h"
#include "timer.h"
#include "wlan.h"

#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zx/time.h>

#include <cinttypes>
#include <cstring>
#include <sstream>

#include "garnet/drivers/wlan/common/bitfield.h"
#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

namespace wlan {

ClientMlme::ClientMlme(DeviceInterface* device) : device_(device) {
    debugfn();
}

ClientMlme::~ClientMlme() {}

zx_status_t ClientMlme::Init() {
    debugfn();

    fbl::unique_ptr<Timer> timer;
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kScanner));
    zx_status_t status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
    if (status != ZX_OK) {
        errorf("could not create scan timer: %d\n", status);
        return status;
    }
    scanner_.reset(new Scanner(device_, std::move(timer)));
    return status;
}

zx_status_t ClientMlme::HandleTimeout(const ObjectId id) {
    switch (id.target()) {
    case to_enum_type(ObjectTarget::kScanner):
        scanner_->HandleTimeout();
        break;
    case to_enum_type(ObjectTarget::kStation):
        // TODO(porce): Fix this crash point. bssid() can be nullptr.
        if (id.mac() != sta_->bssid()->ToU64()) {
            warnf("timeout for unknown bssid: %s (%" PRIu64 ")\n", MACSTR(*(sta_->bssid())),
                  id.mac());
            break;
        }
        sta_->HandleTimeout();
        break;
    default:
        ZX_DEBUG_ASSERT(false);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

zx_status_t ClientMlme::HandleNullDataFrame(const DataFrameHeader* hdr,
                                            const wlan_rx_info_t* rxinfo) {
    debugfn();

    if (IsStaValid() && *sta_->bssid() == hdr->addr2) {
        return sta_->HandleNullDataFrame(hdr, rxinfo);
    }

    return ZX_OK;
}

zx_status_t ClientMlme::HandleDataFrame(const DataFrame<LlcHeader>* frame,
                                        const wlan_rx_info_t* rxinfo) {
    debugfn();

    if (IsStaValid() && *sta_->bssid() == frame->hdr->addr2) {
        return sta_->HandleDataFrame(frame, rxinfo);
    }

    return ZX_OK;
}

zx_status_t ClientMlme::HandleEthFrame(const BaseFrame<EthernetII>* frame) {
    debugfn();

    return IsStaValid() ? sta_->HandleEthFrame(frame) : ZX_OK;
}

// TODO(tkilbourn): send error response back to service is !IsStaValid (for all MLME requests)
zx_status_t ClientMlme::HandleMlmeScanReq(ScanRequestPtr req) {
    debugfn();

    return scanner_->Start(std::move(req));
}

zx_status_t ClientMlme::HandleMlmeJoinReq(JoinRequestPtr req) {
    debugfn();

    fbl::unique_ptr<Timer> timer;
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kStation));
    timer_id.set_mac(common::MacAddr(req->selected_bss->bssid.data()).ToU64());
    auto status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
    if (status != ZX_OK) {
        errorf("could not create station timer: %d\n", status);
        return status;
    }
    sta_.reset(new Station(device_, std::move(timer)));
    return sta_->Join(std::move(req));
}

zx_status_t ClientMlme::HandleMlmeAuthReq(AuthenticateRequestPtr req) {
    debugfn();

    if (IsStaValid()) { return sta_->Authenticate(std::move(req)); }

    return ZX_OK;
}

zx_status_t ClientMlme::HandleMlmeDeauthReq(DeauthenticateRequestPtr req) {
    debugfn();

    if (IsStaValid()) { return sta_->Deauthenticate(std::move(req)); }

    return ZX_OK;
}

zx_status_t ClientMlme::HandleMlmeAssocReq(AssociateRequestPtr req) {
    debugfn();

    if (IsStaValid()) { return sta_->Associate(std::move(req)); }

    return ZX_OK;
}

zx_status_t ClientMlme::HandleMlmeEapolReq(EapolRequestPtr req) {
    debugfn();

    if (IsStaValid()) { return sta_->SendEapolRequest(std::move(req)); }

    return ZX_OK;
}

zx_status_t ClientMlme::HandleMlmeSetKeysReq(SetKeysRequestPtr req) {
    debugfn();

    if (IsStaValid()) { return sta_->SetKeys(std::move(req)); }

    return ZX_OK;
}

zx_status_t ClientMlme::HandleBeacon(const MgmtFrame<Beacon>* frame, const wlan_rx_info_t* rxinfo) {
    debugfn();

    if (scanner_->IsRunning()) { scanner_->HandleBeacon(frame, rxinfo); }

    if (IsStaValid() && *sta_->bssid() == frame->hdr->addr3) { sta_->HandleBeacon(frame, rxinfo); }

    return ZX_OK;
}

zx_status_t ClientMlme::HandleProbeResponse(const MgmtFrame<ProbeResponse>* frame,
                                            const wlan_rx_info_t* rxinfo) {
    debugfn();

    if (scanner_->IsRunning()) { scanner_->HandleProbeResponse(frame, rxinfo); }

    return ZX_OK;
}

zx_status_t ClientMlme::HandleAuthentication(const MgmtFrame<Authentication>* frame,
                                             const wlan_rx_info_t* rxinfo) {
    debugfn();

    if (IsStaValid() && *sta_->bssid() == frame->hdr->addr3) {
        sta_->HandleAuthentication(frame, rxinfo);
    }
    return ZX_OK;
}

zx_status_t ClientMlme::HandleDeauthentication(const MgmtFrame<Deauthentication>* frame,
                                               const wlan_rx_info_t* rxinfo) {
    debugfn();

    if (IsStaValid() && *sta_->bssid() == frame->hdr->addr3) {
        sta_->HandleDeauthentication(frame, rxinfo);
    }
    return ZX_OK;
}

zx_status_t ClientMlme::HandleAssociationResponse(const MgmtFrame<AssociationResponse>* frame,
                                                  const wlan_rx_info_t* rxinfo) {
    debugfn();

    if (IsStaValid() && *sta_->bssid() == frame->hdr->addr3) {
        sta_->HandleAssociationResponse(frame, rxinfo);
    }
    return ZX_OK;
}

zx_status_t ClientMlme::HandleDisassociation(const MgmtFrame<Disassociation>* frame,
                                             const wlan_rx_info_t* rxinfo) {
    debugfn();

    if (IsStaValid() && *sta_->bssid() == frame->hdr->addr3) {
        sta_->HandleDisassociation(frame, rxinfo);
    }
    return ZX_OK;
}

zx_status_t ClientMlme::HandleAddBaRequest(const MgmtFrame<AddBaRequestFrame>* frame,
                                           const wlan_rx_info_t* rxinfo) {
    debugfn();
    if (IsStaValid() && *sta_->bssid() == frame->hdr->addr3) {
        sta_->HandleAddBaRequest(frame, rxinfo);
    }
    return ZX_OK;
}

bool ClientMlme::IsStaValid() const {
    // TODO(porce): Redefine the notion of the station validity.
    return sta_ != nullptr && sta_->bssid() != nullptr;
}

zx_status_t ClientMlme::PreChannelChange(wlan_channel_t chan) {
    debugfn();
    if (IsStaValid()) { sta_->PreChannelChange(chan); }
    return ZX_OK;
}

zx_status_t ClientMlme::PostChannelChange() {
    debugfn();
    if (IsStaValid()) { sta_->PostChannelChange(); }
    return ZX_OK;
}

}  // namespace wlan
