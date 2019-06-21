// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/i2c.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpioimpl.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/protocol/gpioimpl.h>
#include <soc/as370/as370-i2c.h>

#include "as370.h"

namespace board_as370 {

zx_status_t As370::I2cInit() {
    zx_status_t status;

    constexpr uint32_t i2c_gpios[] = {
        as370::kI2c0Sda,
        as370::kI2c0Scl,
        as370::kI2c1Sda,
        as370::kI2c1Scl,
    };

    ddk::GpioImplProtocolClient gpio(parent());
    for (uint32_t i = 0; i < countof(i2c_gpios); i++) {
        status = gpio.SetAltFunction(i2c_gpios[i], 1); // 1 == SDA/SCL pinmux setting.
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: GPIO SetAltFunction failed %d\n", __FUNCTION__, status);
            return status;
        }
    }

    constexpr pbus_mmio_t i2c_mmios[] = {
        {
            .base = as370::kI2c0Base,
            .length = as370::kI2c0Size,
        },
        {
            .base = as370::kI2c1Base,
            .length = as370::kI2c1Size,
        },
    };

    constexpr pbus_irq_t i2c_irqs[] = {
        {
            .irq = as370::kI2c0Irq,
            .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
        },
        {
            .irq = as370::kI2c1Irq,
            .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
        },
    };

    constexpr i2c_channel_t i2c_channels[] = {};

    const pbus_metadata_t i2c_metadata[] = {
        {
            .type = DEVICE_METADATA_I2C_CHANNELS,
            .data_buffer = &i2c_channels,
            .data_size = sizeof(i2c_channels),
        },
    };

    pbus_dev_t i2c_dev = {};
    i2c_dev.name = "i2c";
    i2c_dev.vid = PDEV_VID_GENERIC,
    i2c_dev.pid = PDEV_PID_GENERIC,
    i2c_dev.did = PDEV_DID_DW_I2C;
    i2c_dev.mmio_list = i2c_mmios;
    i2c_dev.mmio_count = countof(i2c_mmios);
    i2c_dev.irq_list = i2c_irqs;
    i2c_dev.irq_count = countof(i2c_irqs);
    i2c_dev.metadata_list = i2c_metadata;
    i2c_dev.metadata_count = countof(i2c_metadata);

    status = pbus_.DeviceAdd(&i2c_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace board_as370
