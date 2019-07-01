// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/buttons.h>
#include <ddk/platform-defs.h>

#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {

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
    {
        // Mic privacy switch.
        .gpio = S905D2_GPIOZ(2),

    },
};

// clang-format off
static const buttons_button_config_t buttons[] = {
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP,   0, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_DOWN, 1, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_FDR,         2, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_MUTE,    3, 0, 0},
};
// No need for internal pull, external pull-ups used.
static const buttons_gpio_config_t gpios[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0                         , {GPIO_NO_PULL}},
};
// clang-format on

static const pbus_metadata_t available_buttons_metadata[] = {
    {
        .type = DEVICE_METADATA_BUTTONS_BUTTONS,
        .data_buffer = &buttons,
        .data_size = sizeof(buttons),
    },
    {
        .type = DEVICE_METADATA_BUTTONS_GPIOS,
        .data_buffer = &gpios,
        .data_size = sizeof(gpios),
    }
};

static pbus_dev_t astro_buttons_dev = []() {
    pbus_dev_t dev;
    dev.name = "astro-buttons";
    dev.vid = PDEV_VID_GENERIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_HID_BUTTONS;
    dev.gpio_list = astro_buttons_gpios;
    dev.gpio_count = countof(astro_buttons_gpios);
    dev.metadata_list = available_buttons_metadata;
    dev.metadata_count = countof(available_buttons_metadata);
    return dev;
}();

zx_status_t Astro::ButtonsInit() {

    zx_status_t status = pbus_.DeviceAdd(&astro_buttons_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed: %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace astro
