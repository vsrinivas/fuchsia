// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ap_mlme.h"
#include "beacon_sender.h"
#include "dispatcher.h"

#include "logging.h"

namespace wlan {

ApMlme::ApMlme(DeviceInterface* device) : device_(device) {}

zx_status_t ApMlme::Init() {
    debugfn();

    // Setup BeaconSender.
    bcn_sender_.reset(new BeaconSender(device_));
    auto status = bcn_sender_->Init();
    if (status != ZX_OK) {
        errorf("could not initialize BeaconSender: %d\n", status);
        return status;
    }

    // Create a BSS. It'll become active once MLME issues a Start request.
    auto& bssid = device_->GetState()->address();
    bss_ = fbl::AdoptRef(new InfraBss(device_, bssid));

    return ZX_OK;
}

zx_status_t ApMlme::HandleTimeout(const ObjectId id) {
    debugfn();

    switch (id.target()) {
    case to_enum_type(ObjectTarget::kBss):{
        common::MacAddr client_addr(id.mac());
        return bss_->HandleTimeout(client_addr);
    }
    default:
        ZX_DEBUG_ASSERT(false);
        break;
    }

    return ZX_OK;
}

zx_status_t ApMlme::HandleMlmeStartReq(const StartRequest& req) {
    debugfn();

    if (bcn_sender_->IsStarted()) {
        errorf("received MLME-START.request while already running\n");
        return ZX_OK;
    }

    bcn_sender_->Start(req);
    AddChildHandler(bss_);

    return ZX_OK;
}

zx_status_t ApMlme::HandleMlmeStopReq(const StopRequest& req) {
    debugfn();

    if (!bcn_sender_->IsStarted()) {
        errorf("received MLME-STOP.request without running\n");
        return ZX_OK;
    }

    bcn_sender_->Stop();
    RemoveChildHandler(bss_);

    return ZX_OK;
}

zx_status_t ApMlme::PreChannelChange(wlan_channel_t chan) {
    debugfn();
    // TODO(hahnr): Implement.
    return ZX_OK;
}

zx_status_t ApMlme::PostChannelChange() {
    debugfn();
    // TODO(hahnr): Implement.
    return ZX_OK;
}

}  // namespace wlan
