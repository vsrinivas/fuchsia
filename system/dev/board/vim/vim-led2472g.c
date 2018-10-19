// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

static const pbus_i2c_channel_t led2472g_channels[] = {
    {
        .bus_id = 0,
        .address = 0x46,
    },
};

static const pbus_dev_t led2472g_dev = {
    .name = "led2472g",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_LED2472G,
    .i2c_channel_list = led2472g_channels,
    .i2c_channel_count = countof(led2472g_channels),
};

zx_status_t vim_led2472g_init(vim_bus_t* bus) {
    zx_status_t status;
    if ((status = pbus_device_add(&bus->pbus, &led2472g_dev)) != ZX_OK) {
        zxlogf(ERROR, "vim_led2472g_init: pbus_device_add() failed for led2472g: %d\n", status);
        return status;
    }

    return ZX_OK;
}
