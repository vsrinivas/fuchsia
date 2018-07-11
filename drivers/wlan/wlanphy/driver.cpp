// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver.h"

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <zircon/status.h>

#include "device.h"

// Not guarded by a mutex, because it will be valid between .init and .release and nothing else will
// exist outside those two calls.
static async::Loop* loop = nullptr;

extern "C" zx_status_t wlanphy_init(void** out_ctx) {
    loop = new async::Loop;
    zx_status_t status = loop->StartThread("wlanphy-loop");
    if (status != ZX_OK) {
        zxlogf(ERROR, "wlanphy: could not create event loop: %s\n", zx_status_get_string(status));
        delete loop;
        loop = nullptr;
    } else {
        zxlogf(INFO, "wlanphy: event loop started\n");
    }
    return status;
}

extern "C" zx_status_t wlanphy_bind(void* ctx, zx_device_t* device) {
    zxlogf(INFO, "%s\n", __func__);

    wlanphy_impl_protocol_t wlanphy_impl_proto;
    zx_status_t status;
    status = device_get_protocol(device, ZX_PROTOCOL_WLANPHY_IMPL,
                                 static_cast<void*>(&wlanphy_impl_proto));
    if (status != ZX_OK) {
        zxlogf(ERROR, "wlanphy: bind: no wlanphy_impl protocol (%s)\n",
               zx_status_get_string(status));
        return ZX_ERR_INTERNAL;
    }

    auto wlanphy_dev = std::make_unique<wlanphy::Device>(device, wlanphy_impl_proto);
    status = wlanphy_dev->Bind();
    if (status != ZX_OK) {
        zxlogf(ERROR, "wlanphy: could not bind: %s\n", zx_status_get_string(status));
    } else {
        // devhost is now responsible for the memory used by wlandev. It will be
        // cleaned up in the Device::Release() method.
        wlanphy_dev.release();
    }
    return status;
}

async_t* wlanphy_async_t() {
    return loop->async();
}
