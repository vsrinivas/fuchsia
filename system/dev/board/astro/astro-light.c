// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>

#include <soc/aml-s905d2/s905d2-gpio.h>

#include "astro.h"

static const pbus_i2c_channel_t tcs3400_light_i2c[] = {
    {
        .bus_id = ASTRO_I2C_A0_0,
        .address = I2C_AMBIENTLIGHT_ADDR,
    },
};

static const pbus_gpio_t tcs3400_light_gpios[] = {
    {
        // interrupt
        .gpio = S905D2_GPIOAO(5),
    },
};

static pbus_dev_t tcs3400_light_dev = {
    .name = "tcs3400-light",
    .vid = PDEV_VID_AMS,
    .pid = PDEV_PID_AMS_TCS3400,
    .did = PDEV_DID_AMS_LIGHT,
    .i2c_channels = tcs3400_light_i2c,
    .i2c_channel_count = countof(tcs3400_light_i2c),
    .gpios = tcs3400_light_gpios,
    .gpio_count = countof(tcs3400_light_gpios),
};

zx_status_t ams_light_init(aml_bus_t* bus) {

    zx_status_t status = pbus_device_add(&bus->pbus, &tcs3400_light_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ams_light_init(tcs-3400): pbus_device_add failed: %d\n", status);
        return status;
    }

    return ZX_OK;
}
