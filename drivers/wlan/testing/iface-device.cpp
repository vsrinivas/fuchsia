// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iface-device.h"

#include <ddk/debug.h>

#include <stdio.h>

namespace wlan {
namespace testing {

#define DEV(c) static_cast<IfaceDevice*>(c)
static zx_protocol_device_t wlanmac_test_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
};

static wlanmac_protocol_ops_t wlanmac_test_protocol_ops = {
    .query = [](void* ctx, uint32_t options, wlanmac_info_t* info) -> zx_status_t {
        return DEV(ctx)->Query(options, info);
    },
    .start = [](void* ctx, wlanmac_ifc_t* ifc, void* cookie) -> zx_status_t {
        return DEV(ctx)->Start(ifc, cookie);
    },
    .stop = [](void* ctx) { DEV(ctx)->Stop(); },
    .queue_tx = [](void* ctx, uint32_t options, wlan_tx_packet_t* pkt) -> zx_status_t {
        return ZX_OK;
    },
    .set_channel = [](void* ctx, uint32_t options, wlan_channel_t* chan) -> zx_status_t {
        return DEV(ctx)->SetChannel(options, chan);
    },
    .configure_bss = [](void* ctx, uint32_t options, wlan_bss_config_t* config) -> zx_status_t {
        return ZX_OK;
    },
    .enable_beaconing = [](void* ctx, uint32_t options, bool enabled) -> zx_status_t {
        return ZX_OK;
    },
    .configure_beacon = [](void* ctx, uint32_t options, wlan_tx_packet_t* pkt) -> zx_status_t {
        return ZX_OK;
    },
    .set_key = [](void* ctx, uint32_t options, wlan_key_config_t* key_config) -> zx_status_t {
        return ZX_OK;
    },
};
#undef DEV

IfaceDevice::IfaceDevice(zx_device_t* device, uint16_t role) : parent_(device), role_(role) {}

zx_status_t IfaceDevice::Bind() {
    zxlogf(INFO, "wlan::testing::IfaceDevice::Bind()\n");

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "wlanmac-test";
    args.ctx = this;
    args.ops = &wlanmac_test_device_ops;
    args.proto_id = ZX_PROTOCOL_WLANMAC;
    args.proto_ops = &wlanmac_test_protocol_ops;

    zx_status_t status = device_add(parent_, &args, &zxdev_);
    if (status != ZX_OK) { zxlogf(INFO, "wlan-test: could not add test device: %d\n", status); }
    return status;
}

void IfaceDevice::Unbind() {
    zxlogf(INFO, "wlan::testing::IfaceDevice::Unbind()\n");
    device_remove(zxdev_);
}

void IfaceDevice::Release() {
    zxlogf(INFO, "wlan::testing::IfaceDevice::Release()\n");
    delete this;
}

zx_status_t IfaceDevice::Query(uint32_t options, wlanmac_info_t* info) {
    zxlogf(INFO, "wlan::testing::IfaceDevice::Query()\n");
    memset(info, 0, sizeof(*info));

    static uint8_t mac[ETH_MAC_SIZE] = {0x02, 0x02, 0x02, 0x03, 0x03, 0x03};
    std::memcpy(info->mac_addr, mac, ETH_MAC_SIZE);

    // Fill out a minimal set of wlan device capabilities
    info->supported_phys = WLAN_PHY_DSSS | WLAN_PHY_CCK | WLAN_PHY_OFDM | WLAN_PHY_HT;
    info->driver_features = WLAN_DRIVER_FEATURE_SYNTH;
    info->mac_role = role_;
    info->caps = 0;
    info->num_bands = 2;
    // clang-format off
    info->bands[0] = {
        .desc = "2.4 GHz",
        .ht_caps = {},
        .vht_supported = false,
        .vht_caps = {},
        .basic_rates = {2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108},
        .supported_channels =
            {
                .base_freq = 2417,
                .channels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
            },
    };
    info->bands[1] = {
        .desc = "5 GHz",
        .ht_caps = {},
        .vht_supported = false,
        .vht_caps = {},
        .basic_rates = {12, 18, 24, 36, 48, 72, 96, 108},
        .supported_channels =
            {
                .base_freq = 5000,
                .channels = {36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165},
            },
    };
    // clang-format on

    return ZX_OK;
}

void IfaceDevice::Stop() {
    zxlogf(INFO, "wlan::testing::IfaceDevice::Stop()\n");
    std::lock_guard<std::mutex> lock(lock_);
    ifc_ = nullptr;
    ifc_cookie_ = nullptr;
}

zx_status_t IfaceDevice::Start(wlanmac_ifc_t* ifc, void* cookie) {
    zxlogf(INFO, "wlan::testing::IfaceDevice::Start()\n");
    std::lock_guard<std::mutex> lock(lock_);
    if (ifc_ != nullptr) {
        return ZX_ERR_ALREADY_BOUND;
    } else {
        ifc_ = ifc;
        ifc_cookie_ = cookie;
    }
    return ZX_OK;
}

zx_status_t IfaceDevice::SetChannel(uint32_t options, wlan_channel_t* chan) {
    zxlogf(INFO, "wlan::testing::IfaceDevice::SetChannel()  chan=%u\n", chan->primary);
    return ZX_OK;
}

}  // namespace testing
}  // namespace wlan
