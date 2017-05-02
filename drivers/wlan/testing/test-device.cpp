// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-device.h"

#include <cstdio>

namespace wlan {
namespace testing {

Device::Device(mx_driver_t* driver, mx_device_t* device, test_protocol_t* test_ops)
  : driver_(driver), test_device_(device), test_ops_(test_ops) {}

mx_status_t Device::Bind() {
    std::printf("wlan::testing::Device::Bind()\n");
    device_ops_.unbind = [](mx_device_t* dev) {
        static_cast<Device*>(dev->ctx)->Unbind();
    };
    device_ops_.release = [](mx_device_t* dev) -> mx_status_t {
        return static_cast<Device*>(dev->ctx)->Release();
    };
    device_ops_.ioctl = [](mx_device_t* dev, uint32_t ops, const void* in_buf, size_t in_len,
                           void* out_buf, size_t out_len) -> ssize_t {
        return static_cast<Device*>(dev->ctx)->Ioctl(ops, in_buf, in_len, out_buf, out_len);
    };
    auto status = device_create("wlan-test", this, &device_ops_, driver_, &device_);
    if (status != NO_ERROR) {
        return status;
    }

    wlanmac_ops_.query = [](mx_device_t* dev, uint32_t options, ethmac_info_t* info) {
        return static_cast<Device*>(dev->ctx)->Query(options, info);
    };
    wlanmac_ops_.stop = [](mx_device_t* dev) {
        static_cast<Device*>(dev->ctx)->Stop();
    };
    wlanmac_ops_.start = [](mx_device_t* dev, wlanmac_ifc_t* ifc, void* cookie) {
        return static_cast<Device*>(dev->ctx)->Start(ifc, cookie);
    };
    wlanmac_ops_.tx = [](mx_device_t* dev, uint32_t options, void* data, size_t length) {
        static_cast<Device*>(dev->ctx)->Send(options, data, length);
    };
    wlanmac_ops_.set_channel = [](mx_device_t* dev, uint32_t options, wlan_channel_t* chan) {
        return static_cast<Device*>(dev->ctx)->SetChannel(options, chan);
    };

    device_set_protocol(device_, MX_PROTOCOL_WLANMAC, &wlanmac_ops_);

    // squash unused member error
    // TODO: use test_ops_ for setting up output and control handles
    (void)test_ops_;

    status = device_add(device_, test_device_);
    if (status != NO_ERROR) {
        device_destroy(device_);
    }
    return status;
}
void Device::Unbind() {
    std::printf("wlan::testing::Device::Unbind()\n");
    device_remove(device_);
}

mx_status_t Device::Release() {
    std::printf("wlan::testing::Device::Release()\n");
    device_destroy(device_);
    delete this;
    return NO_ERROR;
}

ssize_t Device::Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t Device::Query(uint32_t options, ethmac_info_t* info) {
    static uint8_t mac[ETH_MAC_SIZE] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    info->features = ETHMAC_FEATURE_WLAN;
    info->mtu = 1500;
    std::memcpy(info->mac, mac, ETH_MAC_SIZE);
    return NO_ERROR;
}

void Device::Stop() {
    std::lock_guard<std::mutex> lock(lock_);
    ifc_ = nullptr;
    cookie_ = nullptr;
}

mx_status_t Device::Start(wlanmac_ifc_t* ifc, void* cookie) {
    std::lock_guard<std::mutex> lock(lock_);
    if (ifc_) {
        return ERR_ALREADY_BOUND;
    } else {
        ifc_ = ifc;
        cookie_ = cookie;
    }
    return NO_ERROR;
}

void Device::Send(uint32_t options, void* data, size_t length) {

}

mx_status_t Device::SetChannel(uint32_t options, wlan_channel_t* chan) {
    return NO_ERROR;
}

}  // namespace testing
}  // namespace wlan
