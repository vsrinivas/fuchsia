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
        {
            .base = MT8167_IOCFG_BASE,
            .length = MT8167_IOCFG_SIZE,
        },
        {
            .base = MT8167_EINT_BASE,
            .length = MT8167_EINT_SIZE,
        },
    };

    const pbus_irq_t gpio_irqs[] = {
        {
            .irq = MT8167_IRQ_ARM_EINT,
            .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
        },
    };

    pbus_dev_t gpio_dev = {};
    gpio_dev.name = "gpio";
    gpio_dev.vid = PDEV_VID_MEDIATEK;
    gpio_dev.pid = PDEV_PID_MEDIATEK_8167S_REF;
    gpio_dev.did = PDEV_DID_MEDIATEK_GPIO;
    gpio_dev.mmio_list = gpio_mmios;
    gpio_dev.mmio_count = countof(gpio_mmios);
    gpio_dev.irq_list = gpio_irqs;
    gpio_dev.irq_count = countof(gpio_irqs);

    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }
//#define GPIO_TEST
#ifdef GPIO_TEST
    const pbus_gpio_t gpio_test_gpios[] = {
        {
            .gpio = 60, // SDA2, to test gpio_write()
        },
        {
            .gpio = 40, // EINT KPROW0 (key matrix) to test gpio_get_interrupt()
        },
    };

    pbus_dev_t gpio_test_dev = {};
    gpio_test_dev.name = "imx8mevk-gpio-test";
    gpio_test_dev.vid = PDEV_VID_GENERIC;
    gpio_test_dev.pid = PDEV_PID_GENERIC;
    gpio_test_dev.did = PDEV_DID_GPIO_TEST;
    gpio_test_dev.gpio_list = gpio_test_gpios;
    gpio_test_dev.gpio_count = countof(gpio_test_gpios);
    if ((status = pbus_.DeviceAdd(&gpio_test_dev)) != ZX_OK) {
        zxlogf(ERROR, "%s: Could not add gpio_test_dev %d\n", __FUNCTION__, status);
        return status;
    }
#endif

    return ZX_OK;
}

} // namespace board_mt8167
