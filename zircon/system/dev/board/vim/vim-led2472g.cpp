// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

namespace vim {
static const pbus_i2c_channel_t led2472g_channels[] = {
    {
        .bus_id = 0,
        .address = 0x46,
    },
};

zx_status_t Vim::Led2472gInit() {
    zx_status_t status;
    pbus_dev_t led2472g_dev = {};
    led2472g_dev.name = "led2472g";
    led2472g_dev.vid = PDEV_VID_GENERIC;
    led2472g_dev.pid = PDEV_PID_GENERIC;
    led2472g_dev.did = PDEV_DID_LED2472G;
    led2472g_dev.i2c_channel_list = led2472g_channels;
    led2472g_dev.i2c_channel_count = countof(led2472g_channels);

    if ((status = pbus_.DeviceAdd(&led2472g_dev)) != ZX_OK) {
        zxlogf(ERROR, "Led2472gInit: pbus_device_add() failed for led2472g: %d\n", status);
        return status;
    }

    return ZX_OK;
}
} //namespace vim