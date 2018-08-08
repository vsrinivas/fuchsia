// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <ddk/device.h>
#include <net/ethernet.h>
#include <wlan/common/logging.h>
#include <wlan/protocol/ioctl.h>
#include <zircon/status.h>

#include "driver.h"

namespace wlanphy {

namespace wlan_device = ::fuchsia::wlan::device;

Device::Device(zx_device_t* device, wlanphy_impl_protocol_t wlanphy_impl_proto)
    : parent_(device), wlanphy_impl_(wlanphy_impl_proto), dispatcher_(wlanphy_async_t()) {
    debugfn();
    // Assert minimum required functionality from the wlanphy_impl driver
    ZX_ASSERT(wlanphy_impl_.ops != nullptr && wlanphy_impl_.ops->query != nullptr &&
              wlanphy_impl_.ops->create_iface != nullptr &&
              wlanphy_impl_.ops->destroy_iface != nullptr);
}

Device::~Device() {
    debugfn();
}

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t wlanphy_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .ioctl = [](void* ctx, uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                size_t out_len, size_t* out_actual) -> zx_status_t {
        return DEV(ctx)->Ioctl(op, in_buf, in_len, out_buf, out_len, out_actual);
    },
};
#undef DEV

zx_status_t Device::Connect(const void* buf, size_t len) {
    debugfn();
    if (buf == nullptr || len < sizeof(zx_handle_t)) { return ZX_ERR_INVALID_ARGS; }

    zx_handle_t hnd = *static_cast<const zx_handle_t*>(buf);
    zx::channel chan(hnd);

    return dispatcher_.AddBinding(std::move(chan), this);
}

zx_status_t Device::Bind() {
    debugfn();

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "wlanphy";
    args.ctx = this;
    args.ops = &wlanphy_device_ops;
    args.proto_id = ZX_PROTOCOL_WLANPHY;
    zx_status_t status = device_add(parent_, &args, &zxdev_);

    if (status != ZX_OK) {
        errorf("wlanphy: could not add device: %s\n", zx_status_get_string(status));
    }

    return status;
}

zx_status_t Device::Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                          size_t out_len, size_t* out_actual) {
    debugfn();
    switch (op) {
    case IOCTL_WLANPHY_CONNECT:
        return Connect(in_buf, in_len);
    default:
        errorf("ioctl unknown: %0x\n", op);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void Device::Release() {
    debugfn();
    delete this;
}

void Device::Unbind() {
    debugfn();

    // Stop accepting new FIDL requests. Once the dispatcher is shut down,
    // remove the device.
    dispatcher_.InitiateShutdown([this] { device_remove(zxdev_); });
}

static void ConvertPhySupportedPhyInfo(::fidl::VectorPtr<wlan_device::SupportedPhy>* SupportedPhys,
                                       uint16_t supported_phys_mask) {
    SupportedPhys->resize(0);
    if (supported_phys_mask & WLAN_PHY_DSSS) {
        SupportedPhys->push_back(wlan_device::SupportedPhy::DSSS);
    }
    if (supported_phys_mask & WLAN_PHY_CCK) {
        SupportedPhys->push_back(wlan_device::SupportedPhy::CCK);
    }
    if (supported_phys_mask & WLAN_PHY_OFDM) {
        SupportedPhys->push_back(wlan_device::SupportedPhy::OFDM);
    }
    if (supported_phys_mask & WLAN_PHY_HT) {
        SupportedPhys->push_back(wlan_device::SupportedPhy::HT);
    }
    if (supported_phys_mask & WLAN_PHY_VHT) {
        SupportedPhys->push_back(wlan_device::SupportedPhy::VHT);
    }
}

static void ConvertPhyDriverFeaturesInfo(
    ::fidl::VectorPtr<wlan_device::DriverFeature>* DriverFeatures, uint32_t driver_features_mask) {
    DriverFeatures->resize(0);
    if (driver_features_mask & WLAN_DRIVER_FEATURE_SCAN_OFFLOAD) {
        DriverFeatures->push_back(wlan_device::DriverFeature::SCAN_OFFLOAD);
    }
    if (driver_features_mask & WLAN_DRIVER_FEATURE_RATE_SELECTION) {
        DriverFeatures->push_back(wlan_device::DriverFeature::RATE_SELECTION);
    }
}

static void ConvertPhyRolesInfo(::fidl::VectorPtr<wlan_device::MacRole>* MacRoles,
                                uint16_t mac_roles_mask) {
    MacRoles->resize(0);
    if (mac_roles_mask & WLAN_MAC_ROLE_CLIENT) {
        MacRoles->push_back(wlan_device::MacRole::CLIENT);
    }
    if (mac_roles_mask & WLAN_MAC_ROLE_AP) { MacRoles->push_back(wlan_device::MacRole::AP); }
}

static void ConvertPhyCaps(::fidl::VectorPtr<wlan_device::Capability>* Capabilities,
                           uint32_t phy_caps_mask) {
    Capabilities->resize(0);
    if (phy_caps_mask & WLAN_CAP_SHORT_PREAMBLE) {
        Capabilities->push_back(wlan_device::Capability::SHORT_PREAMBLE);
    }
    if (phy_caps_mask & WLAN_CAP_SPECTRUM_MGMT) {
        Capabilities->push_back(wlan_device::Capability::SPECTRUM_MGMT);
    }
    if (phy_caps_mask & WLAN_CAP_SHORT_SLOT_TIME) {
        Capabilities->push_back(wlan_device::Capability::SHORT_SLOT_TIME);
    }
    if (phy_caps_mask & WLAN_CAP_RADIO_MGMT) {
        Capabilities->push_back(wlan_device::Capability::RADIO_MGMT);
    }
}

static void ConvertPhyHTCapabilities(wlan_device::HtCapabilities* HTCaps,
                                     const wlan_ht_caps_t* phy_ht_caps) {
    HTCaps->ht_capability_info = phy_ht_caps->ht_capability_info;
    HTCaps->ampdu_params = phy_ht_caps->ampdu_params;
    size_t phy_mcs_set_size = countof(phy_ht_caps->supported_mcs_set);
    ZX_DEBUG_ASSERT(HTCaps->supported_mcs_set.count() >= phy_mcs_set_size);
    for (size_t index = 0; index < phy_mcs_set_size; index++) {
        HTCaps->supported_mcs_set[index] = phy_ht_caps->supported_mcs_set[index];
    }
    HTCaps->ht_ext_capabilities = phy_ht_caps->ht_ext_capabilities;
    HTCaps->tx_beamforming_capabilities = phy_ht_caps->tx_beamforming_capabilities;
    HTCaps->asel_capabilities = phy_ht_caps->asel_capabilities;
}

static void ConvertPhyVHTCapabilities(wlan_device::BandInfo* Band,
                                      const wlan_vht_caps* phy_vht_caps) {
    Band->vht_caps->vht_capability_info = phy_vht_caps->vht_capability_info;
    Band->vht_caps->supported_vht_mcs_and_nss_set = phy_vht_caps->supported_vht_mcs_and_nss_set;
}

static void ConvertPhyChannels(wlan_device::ChannelList* Channels,
                               const wlan_chan_list_t* phy_channels) {
    // base_freq
    Channels->base_freq = phy_channels->base_freq;

    // channels
    Channels->channels.resize(0);
    size_t channel_ndx = 0;
    while ((channel_ndx < countof(phy_channels->channels)) &&
           (phy_channels->channels[channel_ndx] > 0)) {
        Channels->channels.push_back(phy_channels->channels[channel_ndx]);
        channel_ndx++;
    }
}

static void ConvertPhyBandInfo(::fidl::VectorPtr<wlan_device::BandInfo>* BandInfo,
                               uint8_t num_bands, const wlan_band_info_t* phy_bands) {
    BandInfo->resize(0);
    for (uint8_t band_num = 0; band_num < num_bands; band_num++) {
        wlan_device::BandInfo Band;
        const wlan_band_info_t* phy_band = &phy_bands[band_num];

        // description
        Band.description = phy_band->desc;

        // ht_caps
        ConvertPhyHTCapabilities(&Band.ht_caps, &phy_bands->ht_caps);

        // vht_caps
        if (phy_bands->vht_supported) {
            Band.vht_caps = std::make_unique<wlan_device::VhtCapabilities>();
            ConvertPhyVHTCapabilities(&Band, &phy_bands->vht_caps);
        }

        // basic_rates
        Band.basic_rates.resize(0);
        size_t rate_ndx = 0;
        while ((rate_ndx < countof(phy_bands->basic_rates)) &&
               (phy_bands->basic_rates[rate_ndx] > 0)) {
            Band.basic_rates.push_back(phy_bands->basic_rates[rate_ndx]);
            rate_ndx++;
        }

        // supported_channels
        ConvertPhyChannels(&Band.supported_channels, &phy_bands->supported_channels);

        BandInfo->push_back(std::move(Band));
    }
}

static void ConvertPhyInfo(wlan_device::PhyInfo* info, const wlan_info_t* phy_info) {
    // mac
    memcpy(info->hw_mac_address.mutable_data(), phy_info->mac_addr, ETH_ALEN);

    // supported_phys
    ConvertPhySupportedPhyInfo(&info->supported_phys, phy_info->supported_phys);

    // driver_features
    ConvertPhyDriverFeaturesInfo(&info->driver_features, phy_info->driver_features);

    // mac_roles
    ConvertPhyRolesInfo(&info->mac_roles, phy_info->mac_role);

    // caps
    ConvertPhyCaps(&info->caps, phy_info->caps);

    // bands
    ConvertPhyBandInfo(&info->bands, phy_info->num_bands, phy_info->bands);
}

void Device::Query(QueryCallback callback) {
    debugfn();
    wlan_device::QueryResponse resp;
    wlanphy_info_t phy_info;
    resp.status = wlanphy_impl_.ops->query(wlanphy_impl_.ctx, &phy_info);
    ConvertPhyInfo(&resp.info, &phy_info.wlan_info);
    callback(std::move(resp));
}

void Device::CreateIface(wlan_device::CreateIfaceRequest req, CreateIfaceCallback callback) {
    debugfn();
    wlan_device::CreateIfaceResponse resp;

    uint16_t role = 0;
    switch (req.role) {
    case wlan_device::MacRole::CLIENT:
        role = WLAN_MAC_ROLE_CLIENT;
        break;
    case wlan_device::MacRole::AP:
        role = WLAN_MAC_ROLE_AP;
        break;
    }

    if (role != 0) {
        uint16_t iface_id;
        resp.status = wlanphy_impl_.ops->create_iface(wlanphy_impl_.ctx, role, &iface_id);
        resp.iface_id = iface_id;
    } else {
        resp.status = ZX_ERR_NOT_SUPPORTED;
    }

    callback(std::move(resp));
}

void Device::DestroyIface(wlan_device::DestroyIfaceRequest req, DestroyIfaceCallback callback) {
    debugfn();
    wlan_device::DestroyIfaceResponse resp;
    resp.status = wlanphy_impl_.ops->destroy_iface(wlanphy_impl_.ctx, req.id);
    callback(std::move(resp));
}

}  // namespace wlanphy
