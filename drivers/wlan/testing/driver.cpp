// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "phy-device.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>

#include <memory>
#include <stdio.h>

extern "C" zx_status_t wlanphy_test_bind(void* ctx, zx_device_t* device) {
    printf("%s\n", __func__);

    test_protocol_t proto;
    auto status = device_get_protocol(device, ZX_PROTOCOL_TEST, reinterpret_cast<void*>(&proto));
    if (status != ZX_OK) { return status; }

    auto dev = std::make_unique<wlan::testing::PhyDevice>(device);
    status = dev->Bind();
    if (status != ZX_OK) {
        printf("wlanphy-test: could not bind: %d\n", status);
    } else {
        // devhost is now responsible for the memory used by wlan-test. It will
        // be cleaned up in the Device::Release() method.
        dev.release();
    }

    return status;
}
