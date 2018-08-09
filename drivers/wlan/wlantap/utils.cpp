// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <ddk/protocol/ethernet.h>
#include <fuchsia/wlan/device/cpp/fidl.h>
#include <lib/fxl/arraysize.h>
#include <wlan/protocol/info.h>

namespace wlan_device = ::fuchsia::wlan::device;

uint16_t ConvertSupportedPhys(const ::fidl::VectorPtr<wlan_device::SupportedPhy>& phys) {
    uint16_t ret = 0;
    for (auto sp : *phys) {
        switch (sp) {
        case wlan_device::SupportedPhy::DSSS:
            ret |= WLAN_PHY_DSSS;
            break;
        case wlan_device::SupportedPhy::CCK:
            ret |= WLAN_PHY_CCK;
            break;
        case wlan_device::SupportedPhy::OFDM:
            ret |= WLAN_PHY_OFDM;
            break;
        case wlan_device::SupportedPhy::HT:
            ret |= WLAN_PHY_HT;
            break;
        case wlan_device::SupportedPhy::VHT:
            ret |= WLAN_PHY_VHT;
            break;
        }
    }
    return ret;
}

uint32_t ConvertDriverFeatures(const ::fidl::VectorPtr<wlan_device::DriverFeature>& dfs) {
    uint32_t ret = 0;
    for (auto df : *dfs) {
        switch (df) {
        case wlan_device::DriverFeature::SCAN_OFFLOAD:
            ret |= WLAN_DRIVER_FEATURE_SCAN_OFFLOAD;
            break;
        case wlan_device::DriverFeature::RATE_SELECTION:
            ret |= WLAN_DRIVER_FEATURE_RATE_SELECTION;
            break;
        case wlan_device::DriverFeature::SYNTH:
            ret |= WLAN_DRIVER_FEATURE_SYNTH;
            break;
        }
    }
    return ret;
}

uint16_t ConvertMacRole(wlan_device::MacRole role) {
    switch (role) {
    case wlan_device::MacRole::AP:
        return WLAN_MAC_ROLE_AP;
    case wlan_device::MacRole::CLIENT:
        return WLAN_MAC_ROLE_CLIENT;
    }
}

wlan_device::MacRole ConvertMacRole(uint16_t role) {
    switch (role) {
    case WLAN_MAC_ROLE_AP:
        return wlan_device::MacRole::AP;
    case WLAN_MAC_ROLE_CLIENT:
        return wlan_device::MacRole::CLIENT;
    }
    ZX_ASSERT(0);
}

uint16_t ConvertMacRoles(const ::fidl::VectorPtr<wlan_device::MacRole>& roles) {
    uint16_t ret = 0;
    for (auto role : *roles) {
        ret |= ConvertMacRole(role);
    }
    return ret;
}

uint32_t ConvertCaps(const ::fidl::VectorPtr<wlan_device::Capability>& caps) {
    uint32_t ret = 0;
    for (auto cap : *caps) {
        switch (cap) {
        case wlan_device::Capability::SHORT_PREAMBLE:
            ret |= WLAN_CAP_SHORT_PREAMBLE;
            break;
        case wlan_device::Capability::SPECTRUM_MGMT:
            ret |= WLAN_CAP_SPECTRUM_MGMT;
            break;
        case wlan_device::Capability::SHORT_SLOT_TIME:
            ret |= WLAN_CAP_SHORT_SLOT_TIME;
            break;
        case wlan_device::Capability::RADIO_MGMT:
            ret |= WLAN_CAP_RADIO_MGMT;
            break;
        }
    }
    return ret;
}

void ConvertBandInfo(const wlan_device::BandInfo& in, wlan_band_info_t* out) {
    memset(out, 0, sizeof(*out));
    strlcpy(out->desc, in.description->c_str(), sizeof(out->desc));

    out->ht_caps.ht_capability_info = in.ht_caps.ht_capability_info;
    out->ht_caps.ampdu_params = in.ht_caps.ampdu_params;
    memcpy(out->ht_caps.supported_mcs_set, in.ht_caps.supported_mcs_set.data(),
           sizeof(out->ht_caps.supported_mcs_set));
    out->ht_caps.ht_ext_capabilities = in.ht_caps.ht_ext_capabilities;
    out->ht_caps.tx_beamforming_capabilities = in.ht_caps.tx_beamforming_capabilities;
    out->ht_caps.asel_capabilities = in.ht_caps.asel_capabilities;

    out->vht_supported = (in.vht_caps != nullptr);
    if (in.vht_caps) {
        out->vht_caps.vht_capability_info = in.vht_caps->vht_capability_info;
        out->vht_caps.supported_vht_mcs_and_nss_set = in.vht_caps->supported_vht_mcs_and_nss_set;
    }

    std::copy_n(in.basic_rates->data(),
                std::min(in.basic_rates->size(), arraysize(out->basic_rates)), out->basic_rates);

    out->supported_channels.base_freq = in.supported_channels.base_freq;
    std::copy_n(in.supported_channels.channels->data(),
                std::min(in.supported_channels.channels->size(),
                         arraysize(out->supported_channels.channels)),
                out->supported_channels.channels);
}

zx_status_t ConvertPhyInfo(wlan_info_t* out, const wlan_device::PhyInfo& in) {
    std::memset(out, 0, sizeof(*out));
    std::copy_n(in.hw_mac_address.begin(), ETH_MAC_SIZE, out->mac_addr);
    out->supported_phys = ConvertSupportedPhys(in.supported_phys);
    out->driver_features = ConvertDriverFeatures(in.driver_features);
    out->mac_role = ConvertMacRoles(in.mac_roles);
    out->caps = ConvertCaps(in.caps);
    out->num_bands = std::min(in.bands->size(), static_cast<size_t>(WLAN_MAX_BANDS));
    for (size_t i = 0; i < out->num_bands; ++i) {
        ConvertBandInfo((*in.bands)[i], &out->bands[i]);
    }
    return ZX_OK;
}

