// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-device.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>

#include <cstdio>
#include <memory>

extern "C" zx_status_t wlan_test_bind(void* ctx, zx_device_t* device, void** cookie) {
    std::printf("%s\n", __func__);

    test_protocol_t proto;
    auto status = device_get_protocol(device, ZX_PROTOCOL_TEST, reinterpret_cast<void*>(&proto));
    if (status != ZX_OK) { return status; }

    auto dev = std::make_unique<wlan::testing::Device>(device, &proto);
    status = dev->Bind();
    if (status != ZX_OK) {
        std::printf("wlan-test: could not bind: %d\n", status);
    } else {
        // devhost is now responsible for the memory used by wlan-test. It will
        // be cleaned up in the Device::Release() method.
        dev.release();
    }

    return status;
}
