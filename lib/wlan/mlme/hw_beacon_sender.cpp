// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/hw_beacon_sender.h>

#include <wlan/common/logging.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

#include <zircon/assert.h>

namespace wlan {

HwBeaconSender::HwBeaconSender(DeviceInterface* device) : device_(device) {}

zx_status_t HwBeaconSender::Init() {
    return ZX_OK;
}

zx_status_t HwBeaconSender::Start(const StartRequest& req) {
    debugfn();
    ZX_DEBUG_ASSERT(!started_);

    started_ = true;
    return SendBeaconFrame(req);
}

zx_status_t HwBeaconSender::Stop() {
    debugfn();
    ZX_DEBUG_ASSERT(started_);

    started_ = false;
    // TODO(hahnr): Let hardware know there is no need for sending Beacon frames anymore.
    return ZX_OK;
}

bool HwBeaconSender::IsStarted() {
    debugfn();
    return started_;
}

zx_status_t HwBeaconSender::SendBeaconFrame(const StartRequest& req) {
    debugfn();

    // TODO(hahnr): Length of elements is not known at this time. Allocate enough bytes.
    // This should be updated once there is a better size management.
    size_t body_payload_len = 128;
    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<Beacon>(&packet, body_payload_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    const common::MacAddr& bssid = device_->GetState()->address();
    hdr->addr1 = common::kBcastMac;
    hdr->addr2 = bssid;
    hdr->addr3 = bssid;
    FillTxInfo(&packet, *hdr);

    auto bcn = frame.body;
    bcn->beacon_interval = req.beacon_period;
    bcn->timestamp = 0;
    bcn->cap.set_ess(1);
    bcn->cap.set_short_preamble(1);

    // Write elements.
    ElementWriter w(bcn->elements, body_payload_len);
    if (!w.write<SsidElement>(req.ssid.data())) {
        errorf("[hw-bcn-sender] could not write ssid \"%s\" to Beacon\n", req.ssid.data());
        return ZX_ERR_IO;
    }

    // TODO(hahnr): Query from hardware which IEs must be filled out here.
    // TODO(hahnr): Write TIM.

    // Validate the request in debug mode.
    ZX_DEBUG_ASSERT(bcn->Validate(w.size()));

    // Update the length with final values
    body_payload_len = w.size();
    size_t actual_len = hdr->len() + sizeof(Beacon) + body_payload_len;
    auto status = packet->set_len(actual_len);
    if (status != ZX_OK) {
        errorf("[hw-bcn-sender] could not set packet length to %zu: %d\n", actual_len, status);
        return status;
    }

    // TODO(hahnr): Use dedicated device and DDK path to configure Beacons in the driver rather than
    // shortcutting via the tx path.
    status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("[hw-bcn-sender] could not send beacon packet: %d\n", status);
        return status;
    }

    return ZX_OK;
}

}  // namespace wlan
