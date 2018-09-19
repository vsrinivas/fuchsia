// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>

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
        .bus_id = 1,
        .address = 0x38,
    },
};

static pbus_dev_t ft3x27_touch_dev = {
    .name = "ft3x27-touch",
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_ASTRO,
    .did = PDEV_DID_ASTRO_FOCALTOUCH,
    .i2c_channels = ft3x27_touch_i2c,
    .i2c_channel_count = countof(ft3x27_touch_i2c),
    .gpios = touch_gpios,
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
        zxlogf(INFO, "Innolux/Goodix screen not supported at this time\n");
        return ZX_OK;
    } else {
        zx_status_t status = pbus_device_add(&bus->pbus, &ft3x27_touch_dev);
        if (status != ZX_OK) {
            zxlogf(ERROR, "astro_touch_init(ft3x27): pbus_device_add failed: %d\n", status);
            return status;
        }
    }

    return ZX_OK;
}
