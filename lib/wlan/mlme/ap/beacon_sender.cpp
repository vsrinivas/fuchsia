// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/beacon_sender.h>

#include <wlan/common/logging.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/ap/tim.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>

#include "lib/wlan/fidl/wlan_mlme.fidl.h"

#include <zircon/assert.h>

namespace wlan {

BeaconSender::BeaconSender(DeviceInterface* device) : device_(device) {}

BeaconSender::~BeaconSender() {
    // Ensure Beaconing is stopped when the object is destroyed.
    Stop();
}

void BeaconSender::Start(BssInterface* bss, const StartRequest& req) {
    ZX_DEBUG_ASSERT(!IsStarted());
    bss_ = bss;
    req_ = req.Clone();
    WriteBeacon(nullptr);
    debugbss("[bcn-sender] [%s] started sending Beacons\n", bss_->bssid().ToString().c_str());
}

void BeaconSender::Stop() {
    if (!IsStarted()) { return; }

    auto status = device_->ConfigureBeacon(nullptr);
    if (status != ZX_OK) {
        errorf("[bcn-sender] [%s] could not stop beacon sending: %d\n",
               bss_->bssid().ToString().c_str(), status);
        return;
    }

    debugbss("[bcn-sender] [%s] stopped sending Beacons\n", bss_->bssid().ToString().c_str());
    bss_ = nullptr;
}

bool BeaconSender::IsStarted() {
    return bss_ != nullptr;
}

zx_status_t BeaconSender::UpdateBeacon(const TrafficIndicationMap& tim) {
    debugfn();
    return WriteBeacon(&tim);
}

zx_status_t BeaconSender::WriteBeacon(const TrafficIndicationMap* tim) {
    debugfn();
    ZX_DEBUG_ASSERT(IsStarted());
    if (!IsStarted()) { return ZX_ERR_BAD_STATE; }

    // TODO(hahnr): Length of elements is not known at this time. Allocate enough bytes.
    // This should be updated once there is a better size management.
    size_t body_payload_len = 128;
    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<Beacon>(&packet, body_payload_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    const auto& bssid = bss_->bssid();
    hdr->addr1 = common::kBcastMac;
    hdr->addr2 = bssid;
    hdr->addr3 = bssid;
    FillTxInfo(&packet, *hdr);

    auto bcn = frame.body;
    bcn->beacon_interval = req_->beacon_period;
    bcn->timestamp = bss_->timestamp();
    bcn->cap.set_ess(1);
    bcn->cap.set_short_preamble(1);

    // Write elements.
    ElementWriter w(bcn->elements, body_payload_len);
    if (!w.write<SsidElement>(req_->ssid->data())) {
        // TODO(hahnr): Also log BSSID once BeaconSender has access to its BSS.
        errorf("[bcn-sender] [%s] could not write ssid \"%s\" to Beacon\n",
               bssid.ToString().c_str(), req_->ssid->data());
        return ZX_ERR_IO;
    }

    // Rates (in Mbps): 1 (basic), 2 (basic), 5.5 (basic), 6, 9, 11 (basic), 12, 18
    std::vector<uint8_t> rates = {0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24};
    if (!w.write<SupportedRatesElement>(std::move(rates))) {
        errorf("[bcn-sender] [%s] could not write supported rates\n", bssid.ToString().c_str());
        return ZX_ERR_IO;
    }

    if (!w.write<DsssParamSetElement>(req_->channel)) {
        errorf("[bcn-sender] [%s] could not write DSSS parameters\n", bssid.ToString().c_str());
        return ZX_ERR_IO;
    }

    if (tim) {
        size_t bmp_len;
        uint8_t bmp_offset;
        auto status = tim->WritePartialVirtualBitmap(pvb_, sizeof(pvb_), &bmp_len, &bmp_offset);
        if (status != ZX_OK) {
            errorf("[bcn-sender] [%s] could not write Partial Virtual Bitmap: %d\n",
                   bssid.ToString().c_str(), status);
            return status;
        }

        // TODO(NET-579): Add support for DTIM count. For now always send DTIMs with no BU.
        uint8_t dtim_count = 0;
        uint8_t dtim_period = req_->dtim_period;
        BitmapControl bmp_ctrl;
        bmp_ctrl.set_offset(bmp_offset);
        // TODO(NET-579): Write group traffic indication to bitmap control.
        if (!w.write<TimElement>(dtim_count, dtim_period, bmp_ctrl, pvb_, bmp_len)) {
            errorf("[bcn-sender] [%s] could not write TIM element\n", bssid.ToString().c_str());
            return ZX_ERR_IO;
        }
    }

    // Rates (in Mbps): 24, 36, 48, 54
    std::vector<uint8_t> ext_rates = {0x30, 0x48, 0x60, 0x6c};
    if (!w.write<ExtendedSupportedRatesElement>(std::move(ext_rates))) {
        errorf("[bcn-sender] [%s] could not write extended supported rates\n",
               bssid.ToString().c_str());
        return ZX_ERR_IO;
    }

    // TODO(hahnr): Query from hardware which IEs must be filled out here.

    // Validate the request in debug mode.
    ZX_DEBUG_ASSERT(bcn->Validate(w.size()));

    // Update the length with final values
    body_payload_len = w.size();
    size_t actual_len = hdr->len() + sizeof(Beacon) + body_payload_len;
    auto status = packet->set_len(actual_len);
    if (status != ZX_OK) {
        errorf("[bcn-sender] [%s] could not set packet length to %zu: %d\n",
               bssid.ToString().c_str(), actual_len, status);
        return status;
    }

    status = device_->ConfigureBeacon(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[bcn-sender] [%s] could not send beacon packet: %d\n", bssid.ToString().c_str(),
               status);
        return status;
    }

    return ZX_OK;
}

}  // namespace wlan
