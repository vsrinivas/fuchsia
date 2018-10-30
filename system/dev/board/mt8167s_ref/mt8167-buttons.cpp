// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/buttons.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>

#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::ButtonsInit() {
    constexpr pbus_gpio_t mt8167_buttons_gpios[] = {
        {
            // KPROW0.
            .gpio = 40,
        },
        {
            // KPROW1.
            .gpio = 41,
        },
        {
            // KPCOL0.
            .gpio = 42,
        },
        {
            // KPCOL1.
            .gpio = 43,
        },
    };
    // clang-format off
    static constexpr buttons_button_config_t buttons[] = {
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_VOLUME_UP,  0, 2, 0},
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_A,      1, 2, 0},
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_M,      0, 3, 0},
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_PLAY_PAUSE, 1, 3, 0},
    };
    static constexpr buttons_gpio_config_t gpios[] = {
        {BUTTONS_GPIO_TYPE_INTERRUPT,     BUTTONS_GPIO_FLAG_INVERTED, {GPIO_PULL_UP}},
        {BUTTONS_GPIO_TYPE_INTERRUPT,     BUTTONS_GPIO_FLAG_INVERTED, {GPIO_PULL_UP}},
        {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, BUTTONS_GPIO_FLAG_INVERTED, {0}           },
        {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, BUTTONS_GPIO_FLAG_INVERTED, {0}           },
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
    pbus_dev_t mt8167_buttons_dev = {};
    mt8167_buttons_dev.name = "mt8167-buttons";
    mt8167_buttons_dev.vid = PDEV_VID_GENERIC;
    mt8167_buttons_dev.pid = PDEV_PID_GENERIC;
    mt8167_buttons_dev.did = PDEV_DID_HID_BUTTONS;
    mt8167_buttons_dev.gpio_list = mt8167_buttons_gpios;
    mt8167_buttons_dev.gpio_count = countof(mt8167_buttons_gpios);
    mt8167_buttons_dev.metadata_list = available_buttons_metadata;
    mt8167_buttons_dev.metadata_count = countof(available_buttons_metadata);

    zx_status_t status = pbus_.DeviceAdd(&mt8167_buttons_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_.DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace board_mt8167
