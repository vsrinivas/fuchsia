// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/beacon_sender.h>

#include <wlan/common/channel.h>
#include <wlan/common/logging.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/ap/tim.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <zircon/assert.h>

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
    ZX_DEBUG_ASSERT(IsStarted());
    ZX_DEBUG_ASSERT(frame);
    ZX_DEBUG_ASSERT(tim_ele_offset);

    size_t reserved_ie_len = 256;
    auto status = CreateMgmtFrame(frame, reserved_ie_len);
    if (status != ZX_OK) { return status; }

    auto hdr = frame->hdr();
    const auto& bssid = bss_->bssid();
    hdr->addr1 = common::kBcastMac;
    hdr->addr2 = bssid;
    hdr->addr3 = bssid;
    frame->FillTxInfo();

    auto bcn = frame->body();
    bcn->beacon_interval = req_.beacon_period;
    bcn->timestamp = bss_->timestamp();
    bcn->cap.set_privacy(!req_.rsne.is_null());
    bcn->cap.set_ess(1);
    bcn->cap.set_short_preamble(1);

    // Write elements.
    ElementWriter w(bcn->elements, reserved_ie_len);
    status = WriteSsid(&w);
    if (status != ZX_OK) { return status; }

    status = WriteSupportedRates(&w);
    if (status != ZX_OK) { return status; }

    status = WriteDsssParamSet(&w);
    if (status != ZX_OK) { return status; }

    // To get the TIM offset in frame, we have to count the header, fixed parameters and tagged
    // parameters before TIM is written.
    *tim_ele_offset = frame->View().body_offset() + bcn->len() + w.size();
    status = WriteTim(&w, ps_cfg);
    if (status != ZX_OK) { return status; }

    status = WriteCountry(&w);
    if (status != ZX_OK) { return status; }

    status = WriteExtendedSupportedRates(&w);
    if (status != ZX_OK) { return status; }

    if (!req_.rsne.is_null()) {
        status = WriteRsne(&w);
        if (status != ZX_OK) { return status; }
    }

    if (bss_->IsHTReady()) {
        status = WriteHtCapabilities(&w);
        if (status != ZX_OK) { return status; }

        status = WriteHtOperation(&w);
        if (status != ZX_OK) { return status; }
    }

    // TODO(hahnr): Query from hardware which IEs must be filled out here.

    // Validate the request in debug mode.
    ZX_DEBUG_ASSERT(bcn->Validate(w.size()));

    // Update the length with final values
    size_t body_len = frame->body()->len() + w.size();
    status = frame->set_body_len(body_len);
    if (status != ZX_OK) {
        errorf("[bcn-sender] [%s] could not set body length to %zu: %d\n", bssid.ToString().c_str(),
               body_len, status);
        return status;
    }

    return status;
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

    // Length of elements is not known at this time. Allocate enough bytes.
    // This should be updated once there is a better size management.
    size_t reserved_ie_len = 256;
    MgmtFrame<ProbeResponse> frame;
    auto status = CreateMgmtFrame(&frame, reserved_ie_len);
    if (status != ZX_OK) { return; }

    auto hdr = frame.hdr();
    const auto& bssid = bss_->bssid();
    hdr->addr1 = probe_req_frame.hdr()->addr2;
    hdr->addr2 = bssid;
    hdr->addr3 = bssid;
    frame.FillTxInfo();

    auto resp = frame.body();
    resp->beacon_interval = static_cast<uint16_t>(req_.beacon_period);
    resp->timestamp = bss_->timestamp();
    resp->cap.set_privacy(!req_.rsne.is_null());
    resp->cap.set_ess(1);
    resp->cap.set_short_preamble(1);

    // Write elements.
    ElementWriter w(resp->elements, reserved_ie_len);
    if (WriteSsid(&w) != ZX_OK) { return; }
    if (WriteSupportedRates(&w) != ZX_OK) { return; }
    if (WriteDsssParamSet(&w) != ZX_OK) { return; }
    if (WriteCountry(&w) != ZX_OK) { return; }
    if (WriteExtendedSupportedRates(&w) != ZX_OK) { return; }
    if (!req_.rsne.is_null() && WriteRsne(&w) != ZX_OK) { return; }

    if (bss_->IsHTReady()) {
        if (WriteHtCapabilities(&w) != ZX_OK) { return; }
        if (WriteHtOperation(&w) != ZX_OK) { return; }
    }

    // TODO(hahnr): Query from hardware which IEs must be filled out here.

    // Validate the request in debug mode.
    ZX_DEBUG_ASSERT(resp->Validate(w.size()));

    // Update the length with final values
    size_t body_len = resp->len() + w.size();
    status = frame.set_body_len(body_len);
    if (status == ZX_OK) {
        status = device_->SendWlan(frame.Take());
        if (status != ZX_OK) {
            errorf("[bcn-sender] [%s] could not send ProbeResponse packet: %d\n",
                   bssid.ToString().c_str(), status);
        }
    } else {
        errorf("[bcn-sender] [%s] could not set body length to %zu: %d\n", bssid.ToString().c_str(),
               body_len, status);
    }
}

zx_status_t BeaconSender::WriteSsid(ElementWriter* w) {
    if (!w->write<SsidElement>(req_.ssid->data(), req_.ssid->size())) {
        errorf("[bcn-sender] [%s] could not write ssid \"%s\" to Beacon\n",
               bss_->bssid().ToString().c_str(), debug::ToAsciiOrHexStr(*req_.ssid).c_str());
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t BeaconSender::WriteSupportedRates(ElementWriter* w) {
    std::vector<SupportedRate> rates = {
        SupportedRate::basic(12), SupportedRate(18), SupportedRate::basic(24), SupportedRate(36),
        SupportedRate::basic(48), SupportedRate(72), SupportedRate(96),        SupportedRate(108)};
    if (!w->write<SupportedRatesElement>(std::move(rates))) {
        errorf("[bcn-sender] [%s] could not write supported rates\n",
               bss_->bssid().ToString().c_str());
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t BeaconSender::WriteDsssParamSet(ElementWriter* w) {
    if (!w->write<DsssParamSetElement>(req_.channel)) {
        errorf("[bcn-sender] [%s] could not write DSSS parameters\n",
               bss_->bssid().ToString().c_str());
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t BeaconSender::WriteTim(ElementWriter* w, const PsCfg& ps_cfg) {
    size_t bitmap_len;
    uint8_t bitmap_offset;
    auto status =
        ps_cfg.GetTim()->WritePartialVirtualBitmap(pvb_, sizeof(pvb_), &bitmap_len, &bitmap_offset);
    if (status != ZX_OK) {
        errorf("[bcn-sender] [%s] could not write Partial Virtual Bitmap: %d\n",
               bss_->bssid().ToString().c_str(), status);
        return status;
    }

    uint8_t dtim_count = ps_cfg.dtim_count();
    uint8_t dtim_period = ps_cfg.dtim_period();
    ZX_DEBUG_ASSERT(dtim_count != dtim_period);
    if (dtim_count == dtim_period) {
        warnf("[bcn-sender] [%s] illegal DTIM state", bss_->bssid().ToString().c_str());
    }

    BitmapControl bmp_ctrl;
    bmp_ctrl.set_offset(bitmap_offset);
    if (ps_cfg.IsDtim()) { bmp_ctrl.set_group_traffic_ind(ps_cfg.GetTim()->HasGroupTraffic()); }
    if (!w->write<TimElement>(dtim_count, dtim_period, bmp_ctrl, pvb_, bitmap_len)) {
        errorf("[bcn-sender] [%s] could not write TIM element\n", bss_->bssid().ToString().c_str());
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t BeaconSender::WriteCountry(ElementWriter* w) {
    // TODO(NET-799): Read from dot11CountryString MIB
    const uint8_t kCountry[3] = {'U', 'S', ' '};

    std::vector<SubbandTriplet> subbands;

    // TODO(porce): Read from the AP's regulatory domain
    if (wlan::common::Is2Ghz(bss_->Chan())) {
        subbands.push_back({1, 11, 36});
    } else {
        subbands.push_back({36, 4, 36});
        subbands.push_back({52, 4, 30});
        subbands.push_back({100, 12, 30});
        subbands.push_back({149, 5, 36});
    }

    if (!w->write<CountryElement>(kCountry, subbands)) {
        errorf("[bcn-sender] [%s] could not write CountryElement\n",
               bss_->bssid().ToString().c_str());
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t BeaconSender::WriteExtendedSupportedRates(ElementWriter* w) {
    std::vector<SupportedRate> ext_rates = {SupportedRate(48), SupportedRate(72), SupportedRate(96),
                                            SupportedRate(108)};
    if (!w->write<ExtendedSupportedRatesElement>(std::move(ext_rates))) {
        errorf("[bcn-sender] [%s] could not write extended supported rates\n",
               bss_->bssid().ToString().c_str());
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t BeaconSender::WriteHtCapabilities(ElementWriter* w) {
    HtCapabilities htc = bss_->BuildHtCapabilities();
    if (!w->write<HtCapabilities>(htc.ht_cap_info, htc.ampdu_params, htc.mcs_set, htc.ht_ext_cap,
                                  htc.txbf_cap, htc.asel_cap)) {
        errorf("[bcn-sender] [%s] could not write HtCapabilities\n",
               bss_->bssid().ToString().c_str());
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t BeaconSender::WriteHtOperation(ElementWriter* w) {
    HtOperation hto = bss_->BuildHtOperation(bss_->Chan());
    if (!w->write<HtOperation>(hto.primary_chan, hto.head, hto.tail, hto.basic_mcs_set)) {
        errorf("[bcn-sender] [%s] could not write HtOperation\n", bss_->bssid().ToString().c_str());
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t BeaconSender::WriteRsne(ElementWriter* w) {
    if (!w->write<RsnElement>(req_.rsne->data(), req_.rsne->size())) {
        errorf("[bcn-sender] [%s] could not write RSNE\n", bss_->bssid().ToString().c_str());
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

#undef CHECK_WRITE

}  // namespace wlan
