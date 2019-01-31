// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::BacklightInit() {
    if (board_info_.vid != PDEV_VID_GOOGLE || board_info_.pid != PDEV_PID_CLEO) {
        return ZX_OK;
    }

    static constexpr pbus_i2c_channel_t sgm37603a_i2cs[] = {
        {
            .bus_id = 2,
            .address = 0x36
        },
    };

    static constexpr pbus_gpio_t sgm37603a_gpios[] = {
        {
            .gpio = MT8167_CLEO_GPIO_LCM_EN
        }
    };

    pbus_dev_t sgm37603a_dev = {};
    sgm37603a_dev.name = "sgm37603a";
    sgm37603a_dev.vid = PDEV_VID_GENERIC;
    sgm37603a_dev.did = PDEV_DID_SG_MICRO_SGM37603A;
    sgm37603a_dev.i2c_channel_list = sgm37603a_i2cs;
    sgm37603a_dev.i2c_channel_count = countof(sgm37603a_i2cs);
    sgm37603a_dev.gpio_list = sgm37603a_gpios;
    sgm37603a_dev.gpio_count = countof(sgm37603a_gpios);

    zx_status_t status = pbus_.DeviceAdd(&sgm37603a_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to add SGM37603A device: %d\n", __FUNCTION__, status);
    }

    return ZX_OK;
}

}  // namespace board_mt8167
