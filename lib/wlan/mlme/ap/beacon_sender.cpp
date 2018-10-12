// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/beacon_sender.h>

#include <wlan/common/channel.h>
#include <wlan/common/logging.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/ap/tim.h>
#include <wlan/mlme/beacon.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <zircon/assert.h>
#include <zircon/status.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

BeaconSender::BeaconSender(DeviceInterface* device) : device_(device) {}

BeaconSender::~BeaconSender() {
    // Ensure Beaconing is stopped when the object is destroyed.
    Stop();
}

void BeaconSender::Start(BssInterface* bss, const PsCfg& ps_cfg,
                         const MlmeMsg<wlan_mlme::StartRequest>& req) {
    ZX_DEBUG_ASSERT(!IsStarted());

    bss_ = bss;
    req.body()->Clone(&req_);

    // Build the template.
    wlan_bcn_config_t bcn_cfg;
    MgmtFrame<Beacon> frame;
    auto status = BuildBeacon(ps_cfg, &frame, &bcn_cfg.tim_ele_offset);
    if (status != ZX_OK) {
        errorf("[bcn-sender] [%s] could not build beacon template: %d\n",
               bss_->bssid().ToString().c_str(), status);
        return;
    }

    // Copy template content.
    auto packet = frame.Take();
    bcn_cfg.tmpl.packet_head.len = packet->len();
    bcn_cfg.tmpl.packet_head.data = packet->mut_data();
    bcn_cfg.beacon_interval = req.body()->beacon_period;
    status = device_->EnableBeaconing(&bcn_cfg);
    if (status != ZX_OK) {
        errorf("[bcn-sender] [%s] could not start beacon sending: %d\n",
               bss_->bssid().ToString().c_str(), status);
        return;
    }

    debugbss("[bcn-sender] [%s] enabled Beacon sending\n", bss_->bssid().ToString().c_str());
}

void BeaconSender::Stop() {
    if (!IsStarted()) { return; }

    auto status = device_->EnableBeaconing(nullptr);
    if (status != ZX_OK) {
        errorf("[bcn-sender] [%s] could not stop beacon sending: %d\n",
               bss_->bssid().ToString().c_str(), status);
        return;
    }

    debugbss("[bcn-sender] [%s] disabled Beacon sending\n", bss_->bssid().ToString().c_str());
    bss_ = nullptr;
}

bool BeaconSender::IsStarted() {
    return bss_ != nullptr;
}

bool BeaconSender::ShouldSendProbeResponse(const MgmtFrameView<ProbeRequest>& probe_req_frame) {
    size_t elt_len = probe_req_frame.body_len() - probe_req_frame.hdr()->len();
    ElementReader reader(probe_req_frame.body()->elements, elt_len);
    while (reader.is_valid()) {
        auto hdr = reader.peek();
        if (hdr == nullptr) {
            // Invalid header and thus corrupted request.
            return false;
        }

        switch (hdr->id) {
        case element_id::kSsid: {
            auto ie = reader.read<SsidElement>();
            if (ie == nullptr) { return false; };
            if (hdr->len == 0) {
                // Always respond to wildcard requests.
                return true;
            }

            // Send ProbeResponse if request was targeted towards this BSS.
            size_t ssid_len = req_.ssid->size();
            bool to_bss =
                (hdr->len == ssid_len) && (memcmp(ie->ssid, req_.ssid->data(), ssid_len) == 0);
            return to_bss;
        }
        default:
            reader.skip(sizeof(ElementHeader) + hdr->len);
            break;
        }
    }

    // Request did not contain an SSID IE and is therefore a wildcard one.
    return true;
}

zx_status_t BeaconSender::BuildBeacon(const PsCfg& ps_cfg, MgmtFrame<Beacon>* frame,
                                      size_t* tim_ele_offset) {
    BeaconConfig c = {
        .bssid = bss_->bssid(),
        .ssid = req_.ssid->data(),
        .ssid_len = req_.ssid->size(),
        .rsne = req_.rsne.is_null() ? nullptr : req_.rsne->data(),
        .rsne_len = req_.rsne->size(),
        .beacon_period = req_.beacon_period,
        .channel = bss_->Chan(), // looks like we are ignoring 'channel' in 'req'. Is that correct?
        .ps_cfg = &ps_cfg,
        .ht = bss_->Ht(),
    };
    return ::wlan::BuildBeacon(c, frame, tim_ele_offset);
}

zx_status_t BeaconSender::UpdateBeacon(const PsCfg& ps_cfg) {
    debugfn();
    ZX_DEBUG_ASSERT(IsStarted());
    if (!IsStarted()) { return ZX_ERR_BAD_STATE; }

    MgmtFrame<Beacon> frame;
    size_t tim_ele_offset;
    BuildBeacon(ps_cfg, &frame, &tim_ele_offset);

    zx_status_t status = device_->ConfigureBeacon(frame.Take());
    if (status != ZX_OK) {
        errorf("[bcn-sender] [%s] could not send beacon packet: %d\n",
               bss_->bssid().ToString().c_str(), status);
        return status;
    }

    return ZX_OK;
}

void BeaconSender::SendProbeResponse(const MgmtFrameView<ProbeRequest>& probe_req_frame) {
    if (!IsStarted()) { return; }
    if (!ShouldSendProbeResponse(probe_req_frame)) { return; }

    BeaconConfig c = {
        .bssid = bss_->bssid(),
        .ssid = req_.ssid->data(),
        .ssid_len = req_.ssid->size(),
        .rsne = req_.rsne.is_null() ? nullptr : req_.rsne->data(),
        .rsne_len = req_.rsne->size(),
        .beacon_period = req_.beacon_period,
        .channel = bss_->Chan(),
        .ps_cfg = nullptr, // no TIM element in probe response
        .ht = bss_->Ht(),
    };

    MgmtFrame<ProbeResponse> frame;
    zx_status_t status = BuildProbeResponse(c, probe_req_frame.hdr()->addr2, &frame);
    if (status != ZX_OK) {
        errorf("could not build a probe response frame: %s\n", zx_status_get_string(status));
        return;
    }

    frame.FillTxInfo();

    status = device_->SendWlan(frame.Take());
    if (status != ZX_OK) {
        errorf("[bcn-sender] [%s] could not send ProbeResponse packet: %d\n",
               bss_->bssid().ToString().c_str(), status);
    }
}

}  // namespace wlan
