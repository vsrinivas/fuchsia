// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-device.h"

#include <cstdio>

namespace wlan {
namespace testing {

Device::Device(zx_device_t* device, test_protocol_t* test_proto)
    : TestBaseDevice(device), test_proxy_(test_proto) {}

zx_status_t Device::Bind() {
    std::printf("wlan::testing::Device::Bind()\n");

    auto status = DdkAdd("wlan-test");
    if (status != ZX_OK) { std::printf("wlan-test: could not add test device: %d\n", status); }
    return status;
}

void Device::DdkUnbind() {
    std::printf("wlan::testing::Device::Unbind()\n");
    ClearAndSetState(DEV_STATE_READABLE | DEV_STATE_WRITABLE, DEV_STATE_HANGUP);
    device_remove(zxdev());
}

void Device::DdkRelease() {
    std::printf("wlan::testing::Device::Release()\n");
    delete this;
}

zx_status_t Device::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                             size_t out_len, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::WlanmacQuery(uint32_t options, wlanmac_info_t* info) {
    std::printf("wlan::testing::Device::WlanmacQuery()\n");
    memset(info, 0, sizeof(*info));

    static uint8_t mac[ETH_MAC_SIZE] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    info->eth_info.features |= ETHMAC_FEATURE_WLAN;
    info->eth_info.mtu = 1500;
    std::memcpy(info->eth_info.mac, mac, ETH_MAC_SIZE);

    // Fill out a minimal set of wlan device capabilities
    info->supported_phys = WLAN_PHY_DSSS | WLAN_PHY_CCK | WLAN_PHY_OFDM | WLAN_PHY_HT_MIXED;
    info->driver_features = 0;
    info->mac_modes = WLAN_MAC_MODE_STA;
    info->caps = 0;
    info->num_bands = 2;
    info->bands[0] = {
        .desc = "2.4 GHz",
        .ht_caps = {},
        .vht_supported = false,
        .vht_caps = {},
        .supported_channels =
            {
                .base_freq = 2417,
                .channels =
                    {
                        1,
                        2,
                        3,
                        4,
                        5,
                        6,
                        7,
                        8,
                        9,
                        10,
                        11,
                    },
            },
    };
    info->bands[1] = {
        .desc = "5 GHz",
        .ht_caps = {},
        .vht_supported = false,
        .vht_caps = {},
        .supported_channels =
            {
                .base_freq = 5000,
                .channels = {36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165},
            },
    };

    return ZX_OK;
}

void Device::WlanmacStop() {
    std::printf("wlan::testing::Device::WlanmacStop()\n");
    std::lock_guard<std::mutex> lock(lock_);
    ClearState(DEV_STATE_READABLE | DEV_STATE_WRITABLE);
    wlanmac_proxy_.reset();
}

zx_status_t Device::WlanmacStart(fbl::unique_ptr<ddk::WlanmacIfcProxy> proxy) {
    std::printf("wlan::testing::Device::WlanmacStart()\n");
    std::lock_guard<std::mutex> lock(lock_);
    SetState(DEV_STATE_READABLE | DEV_STATE_WRITABLE);
    if (wlanmac_proxy_ != nullptr) {
        return ZX_ERR_ALREADY_BOUND;
    } else {
        wlanmac_proxy_.swap(proxy);
    }
    return ZX_OK;
}

zx_status_t Device::WlanmacQueueTx(uint32_t options, wlan_tx_packet_t* pkt) {
    return ZX_OK;
}

zx_status_t Device::WlanmacSetChannel(uint32_t options, wlan_channel_t* chan) {
    std::printf("wlan::testing::Device::WlanmacSetChannel()  chan=%u\n", chan->primary);
    return ZX_OK;
}

zx_status_t Device::WlanmacSetBss(uint32_t options, const uint8_t mac[6], uint8_t type) {
    return ZX_OK;
}

zx_status_t Device::WlanmacConfigureBss(uint32_t options, wlan_bss_config_t* config) {
    return ZX_OK;
}

zx_status_t Device::WlanmacSetKey(uint32_t options, wlan_key_config_t* key_config) {
    return ZX_OK;
}

}  // namespace testing
}  // namespace wlan
