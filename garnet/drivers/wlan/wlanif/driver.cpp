// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver.h"

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <zircon/status.h>

#include "device.h"

extern "C" zx_status_t wlanif_bind(void* ctx, zx_device_t* device) {
    zxlogf(INFO, "%s\n", __func__);

    wlanif_impl_protocol_t wlanif_impl_proto;
    zx_status_t status;
    status = device_get_protocol(device, ZX_PROTOCOL_WLANIF_IMPL,
                                 static_cast<void*>(&wlanif_impl_proto));
    if (status != ZX_OK) {
        zxlogf(ERROR, "wlanif: bind: no wlanif_impl protocol (%s)\n",
               zx_status_get_string(status));
        return ZX_ERR_INTERNAL;
    }

    auto wlanif_dev = std::make_unique<wlanif::Device>(device, wlanif_impl_proto);

    status = wlanif_dev->Bind();
    if (status != ZX_OK) {
        zxlogf(ERROR, "wlanif: could not bind: %s\n", zx_status_get_string(status));
    } else {
        // devhost is now responsible for the memory used by wlandev. It will be
        // cleaned up in the Device::Release() method.
        wlanif_dev.release();
    }
    return status;
}
