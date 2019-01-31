// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

static const pbus_i2c_channel_t backlight_i2c_channels[] = {
    {
        .bus_id = ASTRO_I2C_3,
        .address = I2C_BACKLIGHT_ADDR,
    },
};

static pbus_dev_t backlight_dev = {
    .name = "backlight",
    .vid = PDEV_VID_TI,
    .pid = PDEV_PID_TI_LP8556,
    .did = PDEV_DID_TI_BACKLIGHT,
    .i2c_channel_list = backlight_i2c_channels,
    .i2c_channel_count = countof(backlight_i2c_channels),
};

zx_status_t astro_backlight_init(aml_bus_t* bus) {
    zx_status_t status = pbus_device_add(&bus->pbus, &backlight_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Could not add backlight dev: %d\n", __FUNCTION__, status);
        return status;
    }
    return ZX_OK;
}

