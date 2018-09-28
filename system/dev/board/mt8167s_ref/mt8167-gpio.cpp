// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>

#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::GpioInit() {

    const pbus_mmio_t gpio_mmios[] = {
        {
            .base = MT8167_GPIO_BASE,
            .length = MT8167_GPIO_SIZE,
        },
    };

    pbus_dev_t gpio_dev = {};
    gpio_dev.name = "gpio";
    gpio_dev.vid = PDEV_VID_MEDIATEK;
    gpio_dev.pid = PDEV_PID_MEDIATEK_8167S_REF;
    gpio_dev.did = PDEV_DID_MEDIATEK_GPIO;
    gpio_dev.mmios = gpio_mmios;
    gpio_dev.mmio_count = countof(gpio_mmios);

    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace board_mt8167
