// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/channel.h>
#include <wlan/mlme/beacon.h>

namespace wlan {

static bool WriteSsid(ElementWriter* w, const BeaconConfig& config) {
    if (config.ssid != nullptr) {
        return w->write<SsidElement>(config.ssid, config.ssid_len);
    } else {
        return true;
    }
}

static bool WriteSupportedRates(ElementWriter* w, const BeaconConfig& config) {
    std::vector<SupportedRate> rates = {
        SupportedRate::basic(12), SupportedRate(18), SupportedRate::basic(24), SupportedRate(36),
        SupportedRate::basic(48), SupportedRate(72), SupportedRate(96),        SupportedRate(108)};
    return w->write<SupportedRatesElement>(std::move(rates));
}

static bool WriteDsssParamSet(ElementWriter* w, const BeaconConfig& config) {
    return w->write<DsssParamSetElement>(config.channel.primary);
}

static bool WriteTim(ElementWriter* w, const PsCfg* ps_cfg, size_t* rel_tim_ele_offset) {
    if (!ps_cfg) {
        return true;
    }

    // To get the TIM offset in frame, we have to count the header, fixed parameters and tagged
    // parameters before TIM is written.
    *rel_tim_ele_offset = w->size();

    size_t bitmap_len;
    uint8_t bitmap_offset;
    uint8_t pvb[TimElement::kMaxLenBmp];
    auto status =
        ps_cfg->GetTim()->WritePartialVirtualBitmap(pvb, sizeof(pvb), &bitmap_len, &bitmap_offset);
    if (status != ZX_OK) {
        return false;
    }

    BitmapControl bmp_ctrl;
    bmp_ctrl.set_offset(bitmap_offset);
    if (ps_cfg->IsDtim()) { bmp_ctrl.set_group_traffic_ind(ps_cfg->GetTim()->HasGroupTraffic()); }
    return w->write<TimElement>(
            ps_cfg->dtim_count(), ps_cfg->dtim_period(), bmp_ctrl, pvb, bitmap_len);
}

static bool WriteCountry(ElementWriter* w, const BeaconConfig& config) {
    // TODO(NET-799): Read from dot11CountryString MIB
    const uint8_t kCountry[3] = {'U', 'S', ' '};

    std::vector<SubbandTriplet> subbands;

    // TODO(porce): Read from the AP's regulatory domain
    if (wlan::common::Is2Ghz(config.channel)) {
        subbands.push_back({1, 11, 36});
    } else {
        subbands.push_back({36, 4, 36});
        subbands.push_back({52, 4, 30});
        subbands.push_back({100, 12, 30});
        subbands.push_back({149, 5, 36});
    }

    return w->write<CountryElement>(kCountry, subbands);
}

static bool WriteExtendedSupportedRates(ElementWriter* w) {
    std::vector<SupportedRate> ext_rates = {SupportedRate(48), SupportedRate(72), SupportedRate(96),
                                            SupportedRate(108)};
    return w->write<ExtendedSupportedRatesElement>(std::move(ext_rates));
}

static bool WriteHtCapabilities(ElementWriter* w, const BeaconConfig& config) {
    if (config.ht.ready) {
        auto h = BuildHtCapabilities(config.ht);
        return w->write<HtCapabilities>(h.ht_cap_info, h.ampdu_params,
                h.mcs_set, h.ht_ext_cap, h.txbf_cap, h.asel_cap);
    } else {
        return true;
    }
}

static bool WriteHtOperation(ElementWriter* w, const BeaconConfig& config) {
    if (config.ht.ready) {
        HtOperation hto = BuildHtOperation(config.channel);
        return w->write<HtOperation>(hto.primary_chan, hto.head, hto.tail, hto.basic_mcs_set);
    } else {
        return true;
    }
}

static bool WriteRsne(ElementWriter* w, const BeaconConfig& config) {
    if (config.rsne != nullptr) {
        return w->write<RsnElement>(config.rsne, config.rsne_len);
    } else {
        return true;
    }
}

static bool WriteMeshConfiguration(ElementWriter* w, const BeaconConfig& config) {
    if (config.mesh_config != nullptr) {
        return w->write<MeshConfigurationElement>(*config.mesh_config);
    } else {
        return true;
    }
}

static bool WriteMeshId(ElementWriter* w, const BeaconConfig& config) {
    if (config.mesh_id != nullptr) {
        return w->write<MeshIdElement>(config.mesh_id, config.mesh_id_len);
    } else {
        return true;
    }
}

static bool WriteElements(ElementWriter* w, const BeaconConfig& config, size_t* rel_tim_ele_offset) {
    // TODO(hahnr): Query from hardware which IEs must be filled out here.
    return WriteSsid(w, config)
        && WriteSupportedRates(w, config)
        && WriteDsssParamSet(w, config)
        && WriteTim(w, config.ps_cfg, rel_tim_ele_offset)
        && WriteCountry(w, config)
        && WriteExtendedSupportedRates(w)
        && WriteRsne(w, config)
        && WriteHtCapabilities(w, config)
        && WriteHtOperation(w, config)
        && WriteMeshConfiguration(w, config)
        && WriteMeshId(w, config);
}

template<typename T>
static void SetBssType(T* bcn, BssType bss_type) {
    // IEEE Std 802.11-2016, 9.4.1.4
    switch (bss_type) {
    case BssType::kInfrastructure:
        bcn->cap.set_ess(1);
        bcn->cap.set_ibss(0);
        break;
    case BssType::kIndependent:
        bcn->cap.set_ess(0);
        bcn->cap.set_ibss(1);
        break;
    case BssType::kMesh:
        bcn->cap.set_ess(0);
        bcn->cap.set_ibss(0);
        break;
    }
}

template<typename T>
static zx_status_t BuildBeaconOrProbeResponse(const BeaconConfig& config,
                                              common::MacAddr addr1,
                                              MgmtFrame<T>* buffer,
                                              size_t* tim_ele_offset) {
    constexpr size_t reserved_ie_len = 256;
    auto status = CreateMgmtFrame(buffer, reserved_ie_len);
    if (status != ZX_OK) { return status; }

    auto hdr = buffer->hdr();
    hdr->addr1 = addr1;
    hdr->addr2 = config.bssid;
    hdr->addr3 = config.bssid;

    auto bcn = buffer->body();
    bcn->beacon_interval = config.beacon_period;
    bcn->timestamp = config.timestamp;
    bcn->cap.set_privacy(config.rsne != nullptr);

    SetBssType(bcn, config.bss_type);
    bcn->cap.set_short_preamble(1);

    // Write elements.
    ElementWriter w(bcn->elements, reserved_ie_len);
    size_t rel_tim_ele_offset = SIZE_MAX;
    if (!WriteElements(&w, config, &rel_tim_ele_offset)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    ZX_DEBUG_ASSERT(bcn->Validate(w.size()));

    // Update the length with final values
    size_t body_len = buffer->body()->len() + w.size();
    status = buffer->set_body_len(body_len);
    if (status != ZX_OK) {
        errorf("could not set body length to %zu: %d\n", body_len, status);
        return status;
    }

    if (tim_ele_offset != nullptr) {
        if (rel_tim_ele_offset == SIZE_MAX) {
            // A tim element offset was requested but no element was written
            return ZX_ERR_INVALID_ARGS;
        }
        *tim_ele_offset = buffer->View().body_offset() + bcn->len() + rel_tim_ele_offset;
    }
    return ZX_OK;
}

zx_status_t BuildBeacon(const BeaconConfig& config,
                        MgmtFrame<Beacon>* buffer,
                        size_t* tim_ele_offset) {
    return BuildBeaconOrProbeResponse(config, common::kBcastMac, buffer, tim_ele_offset);
}

zx_status_t BuildProbeResponse(const BeaconConfig& config,
                               common::MacAddr addr1,
                               MgmtFrame<ProbeResponse>* buffer) {
    return BuildBeaconOrProbeResponse(config, addr1, buffer, nullptr);
}

} // namespace wlan
