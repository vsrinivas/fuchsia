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

    // MT8167S_REF
    constexpr pbus_gpio_t mt8167s_ref_pbus_gpios[] = {
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
    static constexpr buttons_button_config_t mt8167s_ref_buttons[] = {
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_VOLUME_UP,  0, 2, 0},
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_A,      1, 2, 0},
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_M,      0, 3, 0},
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_PLAY_PAUSE, 1, 3, 0},
    };
    static constexpr buttons_gpio_config_t mt8167s_ref_gpios[] = {
        {BUTTONS_GPIO_TYPE_INTERRUPT,     BUTTONS_GPIO_FLAG_INVERTED, {GPIO_PULL_UP}},
        {BUTTONS_GPIO_TYPE_INTERRUPT,     BUTTONS_GPIO_FLAG_INVERTED, {GPIO_PULL_UP}},
        {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, BUTTONS_GPIO_FLAG_INVERTED, {0}           },
        {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, BUTTONS_GPIO_FLAG_INVERTED, {0}           },
    };
    // clang-format on
    static constexpr pbus_metadata_t mt8167s_ref_metadata[] = {
        {
            .type = DEVICE_METADATA_BUTTONS_BUTTONS,
            .data_buffer = &mt8167s_ref_buttons,
            .data_size = sizeof(mt8167s_ref_buttons),
        },
        {
            .type = DEVICE_METADATA_BUTTONS_GPIOS,
            .data_buffer = &mt8167s_ref_gpios,
            .data_size = sizeof(mt8167s_ref_gpios),
        }
    };

    // Cleo
    constexpr pbus_gpio_t cleo_pbus_gpios[] = {
        {
            // VOL+. TODO(andresoportus) plumb VOL- through PMIC.
            .gpio = 42,
        },
        {
            // MUTE_MIC.
            .gpio = 23,
        },
    };
    static constexpr buttons_button_config_t cleo_buttons[] = {
        {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
        {BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_MUTE, 1, 0, 0},
    };
    static constexpr buttons_gpio_config_t cleo_gpios[] = {
        {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_PULL_UP}},
        {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
    };
    static constexpr pbus_metadata_t cleo_metadata[] = {
        {
            .type = DEVICE_METADATA_BUTTONS_BUTTONS,
            .data_buffer = &cleo_buttons,
            .data_size = sizeof(cleo_buttons),
        },
        {
            .type = DEVICE_METADATA_BUTTONS_GPIOS,
            .data_buffer = &cleo_gpios,
            .data_size = sizeof(cleo_gpios),
        }
    };

    pbus_dev_t dev = {};
    dev.name = "mt8167-buttons";
    dev.vid = PDEV_VID_GENERIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_HID_BUTTONS;
    if (board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
        dev.gpio_list = mt8167s_ref_pbus_gpios;
        dev.gpio_count = countof(mt8167s_ref_pbus_gpios);
        dev.metadata_list = mt8167s_ref_metadata;
        dev.metadata_count = countof(mt8167s_ref_metadata);
    } else if (board_info_.pid == PDEV_PID_CLEO) {
        dev.gpio_list = cleo_pbus_gpios;
        dev.gpio_count = countof(cleo_pbus_gpios);
        dev.metadata_list = cleo_metadata;
        dev.metadata_count = countof(cleo_metadata);
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = pbus_.DeviceAdd(&dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_.DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace board_mt8167
