// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/ap_mlme.h>

#include <wlan/common/logging.h>
#include <fbl/ref_ptr.h>

namespace wlan {

ApMlme::ApMlme(DeviceInterface* device) : device_(device) {}

zx_status_t ApMlme::Init() {
    debugfn();
    return ZX_OK;
}

zx_status_t ApMlme::HandleTimeout(const ObjectId id) {
    debugfn();

    switch (id.target()) {
    case to_enum_type(ObjectTarget::kBss): {
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

    // Only one BSS can be started at a time.
    if (bss_ != nullptr) {
        errorf("received MLME-START.request with an already running BSS on device: %s\n",
               device_->GetState()->address().ToString().c_str());
        return ZX_OK;
    }

    // Configure BSS in driver.
    auto& bssid = device_->GetState()->address();
    wlan_bss_config_t cfg{
        .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
        .remote = false,
    };
    bssid.CopyTo(cfg.bssid);
    device_->ConfigureBss(&cfg);

    // Create and start BSS.
    auto bcn_sender = fbl::make_unique<BeaconSender>(device_);
    bss_ = fbl::AdoptRef(new InfraBss(device_, fbl::move(bcn_sender), bssid));
    bss_->Start(req);
    AddChildHandler(bss_);

    return ZX_OK;
}

zx_status_t ApMlme::HandleMlmeStopReq(const StopRequest& req) {
    debugfn();

    if (bss_ == nullptr) {
        errorf("received MLME-STOP.request but no BSS is running on device: %s\n",
               device_->GetState()->address().ToString().c_str());
        return ZX_OK;
    }

    // Stop and destroy BSS.
    RemoveChildHandler(bss_);
    bss_->Stop();
    bss_.reset();

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
