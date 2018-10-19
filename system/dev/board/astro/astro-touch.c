// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>

#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <limits.h>

#include "astro.h"

static const pbus_gpio_t touch_gpios[] = {
    {
        // touch interrupt
        .gpio = S905D2_GPIOZ(4),
    },
    {
        // touch reset
        .gpio = S905D2_GPIOZ(9),
    },
};

static const pbus_i2c_channel_t ft3x27_touch_i2c[] = {
    {
        .bus_id = ASTRO_I2C_2,
        .address = 0x38,
    },
};

static pbus_dev_t ft3x27_touch_dev = {
    .name = "ft3x27-touch",
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_ASTRO,
    .did = PDEV_DID_ASTRO_FOCALTOUCH,
    .i2c_channel_list = ft3x27_touch_i2c,
    .i2c_channel_count = countof(ft3x27_touch_i2c),
    .gpio_list = touch_gpios,
    .gpio_count = countof(touch_gpios),
};

static const pbus_i2c_channel_t gt92xx_touch_i2c[] = {
    {
        .bus_id = ASTRO_I2C_2,
        .address = 0x5d,
    },
};

static pbus_dev_t gt92xx_touch_dev = {
    .name = "gt92xx-touch",
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_ASTRO,
    .did = PDEV_DID_ASTRO_GOODIXTOUCH,
    .i2c_channel_list = gt92xx_touch_i2c,
    .i2c_channel_count = countof(gt92xx_touch_i2c),
    .gpio_list = touch_gpios,
    .gpio_count = countof(touch_gpios),
};


zx_status_t astro_touch_init(aml_bus_t* bus) {

    //Check the display ID pin to determine which driver device to add
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOH(5), 0);
    gpio_impl_config_in(&bus->gpio, S905D2_GPIOH(5), GPIO_NO_PULL);
    uint8_t gpio_state;
    /* Two variants of display are supported, one with BOE display panel and
          ft3x27 touch controller, the other with INX panel and Goodix touch
          controller.  This GPIO input is used to identify each.
          Logic 0 for BOE/ft3x27 combination
          Logic 1 for Innolux/Goodix combination
    */
    gpio_impl_read(&bus->gpio, S905D2_GPIOH(5), &gpio_state);
    if (gpio_state) {
        zx_status_t status = pbus_device_add(&bus->pbus, &gt92xx_touch_dev);
        if (status != ZX_OK) {
            zxlogf(INFO, "astro_touch_init(gt92xx): pbus_device_add failed: %d\n", status);
            return status;
        }
    } else {
        zx_status_t status = pbus_device_add(&bus->pbus, &ft3x27_touch_dev);
        if (status != ZX_OK) {
            zxlogf(ERROR, "astro_touch_init(ft3x27): pbus_device_add failed: %d\n", status);
            return status;
        }
    }

    return ZX_OK;
}
