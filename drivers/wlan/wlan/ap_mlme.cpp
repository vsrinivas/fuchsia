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

    // TODO(hahnr): For development only to unblock from SME changes.
    // To be removed soon.
    auto req = StartRequest::New();
    req->ssid = "FUCHSIA-TEST-AP";
    req->beacon_period = 100;
    HandleMlmeStartReq(*req);

    return ZX_OK;
}

zx_status_t ApMlme::HandleTimeout(const ObjectId id) {
    debugfn();
    // TODO(hahnr): Implement.
    return ZX_OK;
}

zx_status_t ApMlme::HandleMlmeStartReq(const StartRequest& req) {
    debugfn();

    bcn_sender_->Start(req);
    // TODO(hahnr): Create BSS instance which manages clients.

    return ZX_OK;
}

zx_status_t ApMlme::HandleMlmeStopReq(const StopRequest& req) {
    debugfn();
    // TODO(hahnr): Implement.
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
