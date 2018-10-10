// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>

#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

static const pbus_gpio_t astro_buttons_gpios[] = {
    {
        // Volume up.
        .gpio = S905D2_GPIOZ(5),
    },
    {
        // Volume down.
        .gpio = S905D2_GPIOZ(6),
    },
    {
        // Both Volume up and down pressed.
        .gpio = S905D2_GPIOAO(10),
    },
};

static pbus_dev_t astro_buttons_dev = {
    .name = "astro-buttons",
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_ASTRO,
    .did = PDEV_DID_ASTRO_BUTTONS,
    .gpios = astro_buttons_gpios,
    .gpio_count = countof(astro_buttons_gpios),
};

zx_status_t astro_buttons_init(aml_bus_t* bus) {

    zx_status_t status = pbus_device_add(&bus->pbus, &astro_buttons_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pbus_device_add failed: %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}
