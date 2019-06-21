// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/buttons.h>
#include <ddk/platform-defs.h>

#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

zx_status_t Sherlock::ButtonsInit() {
    constexpr pbus_gpio_t sherlock_buttons_gpios[] = {
        {
            // Volume up.
            .gpio = T931_GPIOZ(4),
        },
        {
            // Volume down.
            .gpio = T931_GPIOZ(5),
        },
        {
            // Both Volume up and down pressed.
            .gpio = T931_GPIOZ(13),
        },
        {
            // Mic privacy switch.
            .gpio = T931_GPIOH(3),
        },
    };
    // clang-format off
    static constexpr buttons_button_config_t buttons[] = {
        {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP,   0, 0, 0},
        {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_DOWN, 1, 0, 0},
        {BUTTONS_TYPE_DIRECT, BUTTONS_ID_FDR,         2, 0, 0},
        {BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_MUTE,    3, 0, 0},
    };
    // No need for internal pull, external pull-ups used.
    static constexpr buttons_gpio_config_t gpios[] = {
        {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
        {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
        {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
        {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
    };
    // clang-format on
    static constexpr pbus_metadata_t available_buttons_metadata[] = {
        {
            .type = DEVICE_METADATA_BUTTONS_BUTTONS,
            .data_buffer = &buttons,
            .data_size = sizeof(buttons),
        },
        {
            .type = DEVICE_METADATA_BUTTONS_GPIOS,
            .data_buffer = &gpios,
            .data_size = sizeof(gpios),
        }};
    pbus_dev_t sherlock_buttons_dev = {};
    sherlock_buttons_dev.name = "sherlock-buttons";
    sherlock_buttons_dev.vid = PDEV_VID_GENERIC;
    sherlock_buttons_dev.pid = PDEV_PID_GENERIC;
    sherlock_buttons_dev.did = PDEV_DID_HID_BUTTONS;
    sherlock_buttons_dev.gpio_list = sherlock_buttons_gpios;
    sherlock_buttons_dev.gpio_count = countof(sherlock_buttons_gpios);
    sherlock_buttons_dev.metadata_list = available_buttons_metadata;
    sherlock_buttons_dev.metadata_count = countof(available_buttons_metadata);

    zx_status_t status = pbus_.DeviceAdd(&sherlock_buttons_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_.DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace sherlock
