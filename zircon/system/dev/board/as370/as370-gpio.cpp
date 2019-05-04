// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <soc/as370/as370-gpio.h>

#include "as370.h"

namespace board_as370 {

zx_status_t As370::GpioInit() {
    constexpr pbus_mmio_t gpio_mmios[] = {
        {
            .base = as370::kPinmuxBase,
            .length = as370::kPinmuxSize
        },
        {
            .base = as370::kGpio1Base,
            .length = as370::kGpioSize
        },
        {
            .base = as370::kGpio2Base,
            .length = as370::kGpioSize
        },
    };

    pbus_dev_t gpio_dev = {};
    gpio_dev.name = "gpio";
    gpio_dev.vid = PDEV_VID_SYNAPTICS;
    gpio_dev.pid = PDEV_PID_SYNAPTICS_AS370;
    gpio_dev.did = PDEV_DID_SYNAPTICS_GPIO;
    gpio_dev.mmio_list = gpio_mmios;
    gpio_dev.mmio_count = countof(gpio_mmios);

    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ProtocolDeviceAdd failed: %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

}  // namespace board_as370
