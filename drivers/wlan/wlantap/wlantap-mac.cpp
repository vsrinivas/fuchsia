// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wlantap-mac.h"
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <fuchsia/wlan/device/cpp/fidl.h>
#include <lib/fxl/arraysize.h>
#include <wlan/common/channel.h>
#include <wlan/wlanmac-ifc-proxy.h>

namespace wlan {

namespace wlantap = ::fuchsia::wlan::tap;
namespace wlan_device = ::fuchsia::wlan::device;

namespace {

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

struct WlantapMacImpl : WlantapMac {
    WlantapMacImpl(zx_device_t* phy_device, uint16_t id, wlan_device::MacRole role,
                   const wlantap::WlantapPhyConfig* phy_config, Listener* listener)
        : id_(id), role_(role), phy_config_(phy_config), listener_(listener) {}

    static void DdkUnbind(void* ctx) {
        auto& self = *static_cast<WlantapMacImpl*>(ctx);
        self.RemoveDevice();
    }

    static void DdkRelease(void* ctx) { delete static_cast<WlantapMacImpl*>(ctx); }

    // Wlanmac protocol impl

    static zx_status_t WlanmacQuery(void* ctx, uint32_t options, wlanmac_info_t* info) {
        auto& self = *static_cast<WlantapMacImpl*>(ctx);
        wlan_info_t* ifc_info = &info->ifc_info;

        std::copy_n(self.phy_config_->phy_info.hw_mac_address.begin(), ETH_MAC_SIZE,
                    ifc_info->mac_addr);

        ifc_info->supported_phys = ConvertSupportedPhys(self.phy_config_->phy_info.supported_phys);
        ifc_info->driver_features =
            ConvertDriverFeatures(self.phy_config_->phy_info.driver_features);
        ifc_info->mac_role = ConvertMacRole(self.role_);
        ifc_info->caps = ConvertCaps(self.phy_config_->phy_info.caps);
        ifc_info->num_bands =
            std::min(self.phy_config_->phy_info.bands->size(), static_cast<size_t>(WLAN_MAX_BANDS));
        for (size_t i = 0; i < ifc_info->num_bands; ++i) {
            ConvertBandInfo((*self.phy_config_->phy_info.bands)[i], &ifc_info->bands[i]);
        }
        return ZX_OK;
    }

    static zx_status_t WlanmacStart(void* ctx, wlanmac_ifc_t* ifc, void* cookie) {
        auto& self = *static_cast<WlantapMacImpl*>(ctx);
        {
            std::lock_guard<std::mutex> guard(self.lock_);
            if (self.ifc_) { return ZX_ERR_ALREADY_BOUND; }
            self.ifc_ = WlanmacIfcProxy{ifc, cookie};
        }
        self.listener_->WlantapMacStart(self.id_);
        return ZX_OK;
    }

    static void WlanmacStop(void* ctx) {
        auto& self = *static_cast<WlantapMacImpl*>(ctx);
        {
            std::lock_guard<std::mutex> guard(self.lock_);
            self.ifc_ = {};
        }
        self.listener_->WlantapMacStop(self.id_);
    }

    static zx_status_t WlanmacQueueTx(void* ctx, uint32_t options, wlan_tx_packet_t* pkt) {
        auto& self = *static_cast<WlantapMacImpl*>(ctx);
        self.listener_->WlantapMacQueueTx(self.id_, pkt);
        return ZX_OK;
    }

    static zx_status_t WlanmacSetChannel(void* ctx, uint32_t options, wlan_channel_t* chan) {
        auto& self = *static_cast<WlantapMacImpl*>(ctx);
        if (options != 0) { return ZX_ERR_INVALID_ARGS; }
        if (!wlan::common::IsValidChan(*chan)) { return ZX_ERR_INVALID_ARGS; }
        self.listener_->WlantapMacSetChannel(self.id_, chan);
        return ZX_OK;
    }

    static zx_status_t WlanmacConfigureBss(void* ctx, uint32_t options, wlan_bss_config_t* config) {
        auto& self = *static_cast<WlantapMacImpl*>(ctx);
        if (options != 0) { return ZX_ERR_INVALID_ARGS; }
        bool expected_remote = self.role_ == wlan_device::MacRole::CLIENT;
        if (config->remote != expected_remote) { return ZX_ERR_INVALID_ARGS; }
        self.listener_->WlantapMacConfigureBss(self.id_, config);
        return ZX_OK;
    }

    static zx_status_t WlanmacConfigureBeacon(void* ctx, uint32_t options, wlan_tx_packet_t* pkt) {
        if (options != 0) { return ZX_ERR_INVALID_ARGS; }
        if (pkt != nullptr) { return ZX_ERR_NOT_SUPPORTED; }
        return ZX_OK;
    }

    static zx_status_t WlanmacSetKey(void* ctx, uint32_t options, wlan_key_config_t* key_config) {
        auto& self = *static_cast<WlantapMacImpl*>(ctx);
        if (options != 0) { return ZX_ERR_INVALID_ARGS; }
        self.listener_->WlantapMacSetKey(self.id_, key_config);
        return ZX_OK;
    }

    // WlantapMac impl

    virtual void Rx(const std::vector<uint8_t>& data, const wlantap::WlanRxInfo& rx_info) override {
        std::lock_guard<std::mutex> guard(lock_);
        if (ifc_) {
            wlan_rx_info_t converted_info = {.rx_flags = rx_info.rx_flags,
                                             .valid_fields = rx_info.valid_fields,
                                             .phy = rx_info.phy,
                                             .data_rate = rx_info.data_rate,
                                             .chan = {.primary = rx_info.chan.primary,
                                                      .cbw = rx_info.chan.cbw,
                                                      .secondary80 = rx_info.chan.secondary80},
                                             .mcs = rx_info.mcs,
                                             .rssi_dbm = rx_info.rssi_dbm,
                                             .rcpi_dbmh = rx_info.rcpi_dbmh,
                                             .snr_dbh = rx_info.snr_dbh};
            ifc_.Recv(0, &data[0], data.size(), &converted_info);
        }
    }

    virtual void Status(uint32_t status) override {
        std::lock_guard<std::mutex> guard(lock_);
        if (ifc_) { ifc_.Status(status); }
    }

    virtual void RemoveDevice() override {
        {
            std::lock_guard<std::mutex> guard(lock_);
            ifc_ = {};
        }
        device_remove(device_);
    }

    zx_device_t* device_ = nullptr;
    uint16_t id_;
    wlan_device::MacRole role_;
    std::mutex lock_;
    WlanmacIfcProxy ifc_ __TA_GUARDED(lock_);
    const wlantap::WlantapPhyConfig* phy_config_;
    Listener* listener_;
};

}  // namespace

zx_status_t CreateWlantapMac(zx_device_t* parent_phy,
                             const wlan_device::CreateIfaceRequest& request,
                             const wlantap::WlantapPhyConfig* phy_config, uint16_t id,
                             WlantapMac::Listener* listener, WlantapMac** ret) {
    char name[ZX_MAX_NAME_LEN + 1];
    snprintf(name, sizeof(name), "%s-mac%u", device_get_name(parent_phy), id);
    std::unique_ptr<WlantapMacImpl> wlanmac(
        new WlantapMacImpl(parent_phy, id, request.role, phy_config, listener));
    static zx_protocol_device_t device_ops = {.version = DEVICE_OPS_VERSION,
                                              .unbind = &WlantapMacImpl::DdkUnbind,
                                              .release = &WlantapMacImpl::DdkRelease};
    static wlanmac_protocol_ops_t proto_ops = {
        .query = &WlantapMacImpl::WlanmacQuery,
        .start = &WlantapMacImpl::WlanmacStart,
        .stop = &WlantapMacImpl::WlanmacStop,
        .queue_tx = &WlantapMacImpl::WlanmacQueueTx,
        .set_channel = &WlantapMacImpl::WlanmacSetChannel,
        .configure_bss = &WlantapMacImpl::WlanmacConfigureBss,
        .configure_beacon = &WlantapMacImpl::WlanmacConfigureBeacon,
        .set_key = &WlantapMacImpl::WlanmacSetKey,
    };
    device_add_args_t args = {.version = DEVICE_ADD_ARGS_VERSION,
                              .name = name,
                              .ctx = wlanmac.get(),
                              .ops = &device_ops,
                              .proto_id = ZX_PROTOCOL_WLANMAC,
                              .proto_ops = &proto_ops};
    zx_status_t status = device_add(parent_phy, &args, &wlanmac->device_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not add device: %d\n", __func__, status);
        return status;
    }
    // Transfer ownership to devmgr
    *ret = wlanmac.release();
    return ZX_OK;
}

}  // namespace wlan
