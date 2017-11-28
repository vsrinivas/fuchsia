// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ap_mlme.h"
#include "dispatcher.h"

#include "logging.h"

namespace wlan {

ApMlme::ApMlme(DeviceInterface* device) : device_(device) {
    debugfn();
    (void)device_;
}

ApMlme::~ApMlme() {}

zx_status_t ApMlme::Init() {
    debugfn();
    // TODO(hahnr): Implement.
    return ZX_OK;
}

zx_status_t ApMlme::HandleTimeout(const ObjectId id) {
    debugfn();
    // TODO(hahnr): Implement.
    return ZX_OK;
}

zx_status_t ApMlme::HandleMlmeStartReq(const StartRequest& req) {
    debugfn();
    // TODO(hahnr): Implement.
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
