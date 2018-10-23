// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio-impl.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>

#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::I2cInit() {
    gpio_impl_protocol_t gpio_impl;
    zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_GPIO_IMPL, &gpio_impl);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pdev_get_protocol failed %d\n", __FUNCTION__, status);
        return ZX_ERR_NOT_SUPPORTED;
    }

    constexpr uint32_t gpios[] = {
        58, // SDA0_0
        59, // SCL0_0
        52, // SDA1_0
        53, // SCL1_0
        60, // SDA2_0
        61, // SCL2_0
    };
    for (uint32_t i = 0; i < countof(gpios); ++i) {
        gpio_impl_set_alt_function(&gpio_impl, gpios[i], 1); // 1 == SDA/SCL pinmux setting.
    }

    constexpr pbus_mmio_t i2c_mmios[] = {
        {
            .base = MT8167_I2C0_BASE,
            .length = MT8167_I2C0_SIZE,
        },
        {
            .base = MT8167_I2C1_BASE,
            .length = MT8167_I2C1_SIZE,
        },
        {
            .base = MT8167_I2C2_BASE,
            .length = MT8167_I2C2_SIZE,
        },
        // MMIO for clocks.
        // TODO(andresoportus): Move this to a clock driver.
        {
            .base = MT8167_XO_BASE,
            .length = MT8167_XO_SIZE,
        },
    };

    constexpr pbus_irq_t i2c_irqs[] = {
        {
            .irq = MT8167_IRQ_I2C0,
            .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
        },
        {
            .irq = MT8167_IRQ_I2C1,
            .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
        },
        {
            .irq = MT8167_IRQ_I2C2,
            .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
        },
    };

    pbus_dev_t i2c_dev = {};
    i2c_dev.name = "i2c0";
    i2c_dev.vid = PDEV_VID_MEDIATEK;
    i2c_dev.pid = PDEV_PID_MEDIATEK_8167S_REF;
    i2c_dev.did = PDEV_DID_MEDIATEK_I2C;
    i2c_dev.mmio_list = i2c_mmios;
    i2c_dev.mmio_count = countof(i2c_mmios);
    i2c_dev.irq_list = i2c_irqs;
    i2c_dev.irq_count = countof(i2c_irqs);

    status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_I2C_IMPL, &i2c_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace board_mt8167
