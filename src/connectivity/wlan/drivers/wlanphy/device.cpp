// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <ddk/device.h>
#include <net/ethernet.h>
#include <wlan/common/band.h>
#include <wlan/common/channel.h>
#include <wlan/common/element.h>
#include <wlan/common/logging.h>
#include <wlan/protocol/ioctl.h>
#include <zircon/status.h>

#include <fuchsia/wlan/device/c/fidl.h>
#include <fuchsia/wlan/device/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include "src/lib/fxl/arraysize.h"

#include "driver.h"

namespace wlanphy {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_device = ::fuchsia::wlan::device;
namespace wlan_mlme = ::fuchsia::wlan::mlme;

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
    .message = [](void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
        return DEV(ctx)->Message(msg, txn);
    },
};
#undef DEV

zx_status_t Device::Connect(zx::channel request) {
    debugfn();
    return dispatcher_.AddBinding(std::move(request), this);
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

zx_status_t Device::Message(fidl_msg_t* msg, fidl_txn_t* txn) {
    auto connect = [](void* ctx, zx_handle_t request) {
        return static_cast<Device*>(ctx)->Connect(zx::channel(request));
    };
    static const fuchsia_wlan_device_Connector_ops_t ops = {
        .Connect = connect,
    };
    return fuchsia_wlan_device_Connector_dispatch(this, txn, msg, &ops);
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

static void ConvertPhySupportedPhyInfo(::std::vector<wlan_device::SupportedPhy>* SupportedPhys,
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
    ::std::vector<wlan_common::DriverFeature>* DriverFeatures, uint32_t driver_features_mask) {
    DriverFeatures->resize(0);
    if (driver_features_mask & WLAN_DRIVER_FEATURE_SCAN_OFFLOAD) {
        DriverFeatures->push_back(wlan_common::DriverFeature::SCAN_OFFLOAD);
    }
    if (driver_features_mask & WLAN_DRIVER_FEATURE_RATE_SELECTION) {
        DriverFeatures->push_back(wlan_common::DriverFeature::RATE_SELECTION);
    }
    if (driver_features_mask & WLAN_DRIVER_FEATURE_SYNTH) {
        DriverFeatures->push_back(wlan_common::DriverFeature::SYNTH);
    }
    if (driver_features_mask & WLAN_DRIVER_FEATURE_TX_STATUS_REPORT) {
        DriverFeatures->push_back(wlan_common::DriverFeature::RATE_SELECTION);
    }
    if (driver_features_mask & WLAN_DRIVER_FEATURE_TX_STATUS_REPORT) {
        DriverFeatures->push_back(wlan_common::DriverFeature::TX_STATUS_REPORT);
    }
    if (driver_features_mask & WLAN_DRIVER_FEATURE_TEMP_DIRECT_SME_CHANNEL) {
        DriverFeatures->push_back(wlan_common::DriverFeature::TEMP_DIRECT_SME_CHANNEL);
    }
}

static void ConvertPhyRolesInfo(::std::vector<wlan_device::MacRole>* MacRoles,
                                uint16_t mac_roles_mask) {
    MacRoles->resize(0);
    if (mac_roles_mask & WLAN_MAC_ROLE_CLIENT) {
        MacRoles->push_back(wlan_device::MacRole::CLIENT);
    }
    if (mac_roles_mask & WLAN_MAC_ROLE_AP) { MacRoles->push_back(wlan_device::MacRole::AP); }
    if (mac_roles_mask & WLAN_MAC_ROLE_MESH) { MacRoles->push_back(wlan_device::MacRole::MESH); }
}

static void ConvertPhyCaps(::std::vector<wlan_device::Capability>* Capabilities,
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
    if (phy_caps_mask & WLAN_CAP_RADIO_MSMT) {
        Capabilities->push_back(wlan_device::Capability::RADIO_MSMT);
    }
}

static void ConvertPhyChannels(wlan_device::ChannelList* Channels,
                               const wlan_chan_list_t* phy_channels) {
    // base_freq
    Channels->base_freq = phy_channels->base_freq;

    // channels
    Channels->channels.resize(0);
    size_t channel_ndx = 0;
    while ((channel_ndx < arraysize(phy_channels->channels)) &&
           (phy_channels->channels[channel_ndx] > 0)) {
        Channels->channels.push_back(phy_channels->channels[channel_ndx]);
        channel_ndx++;
    }
}

static void ConvertPhyBandInfo(::std::vector<wlan_device::BandInfo>* BandInfo,
                               uint8_t num_bands, const wlan_band_info_t* phy_bands) {
    BandInfo->resize(0);
    for (uint8_t band_num = 0; band_num < num_bands; band_num++) {
        wlan_device::BandInfo Band;
        const wlan_band_info_t* phy_band = &phy_bands[band_num];
        Band.band_id = wlan::common::BandToFidl(phy_band->band_id);

        // ht_caps
        Band.ht_caps = std::make_unique<wlan_mlme::HtCapabilities>(
            ::wlan::HtCapabilities::FromDdk(phy_bands->ht_caps).ToFidl());

        // vht_caps
        if (phy_bands->vht_supported) {
            Band.vht_caps = std::make_unique<wlan_mlme::VhtCapabilities>(
                ::wlan::VhtCapabilities::FromDdk(phy_bands->vht_caps).ToFidl());
        }

        // basic_rates
        Band.basic_rates.resize(0);
        size_t rate_ndx = 0;
        while ((rate_ndx < arraysize(phy_bands->basic_rates)) &&
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
    memcpy(info->hw_mac_address.data(), phy_info->mac_addr, ETH_ALEN);

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
    case wlan_device::MacRole::MESH:
        role = WLAN_MAC_ROLE_MESH;
        break;
    }

    if (role != 0) {
        uint16_t iface_id;
        resp.status = wlanphy_impl_.ops->create_iface(wlanphy_impl_.ctx, wlanphy_create_iface_req_t{
                .role = role,
                .sme_channel = req.sme_channel.release(),
        }, &iface_id);
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
