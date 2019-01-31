// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include <soc/aml-s905x/s905x-gpio.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include <limits.h>

#include "vim.h"

namespace vim {
// S905X and S912 have same MMIO addresses
static const pbus_mmio_t gpio_mmios[] = {
    {
        .base = S912_GPIO_BASE,
        .length = S912_GPIO_LENGTH,
    },
    {
        .base = S912_GPIO_AO_BASE,
        .length = S912_GPIO_AO_LENGTH,
    },
    {
        .base = S912_GPIO_INTERRUPT_BASE,
        .length = S912_GPIO_INTERRUPT_LENGTH,
    },
};

// S905X and S912 have same GPIO IRQ numbers
static const pbus_irq_t gpio_irqs[] = {
    {
        .irq = S912_GPIO_IRQ_0,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_1,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_2,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_3,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_4,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_5,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_6,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_7,
        .mode = 0,
    },
    {
        .irq = S912_AO_GPIO_IRQ_0,
        .mode = 0,
    },
    {
        .irq = S912_AO_GPIO_IRQ_1,
        .mode = 0,
    },
};

zx_status_t Vim::GpioInit() {

    pbus_dev_t gpio_dev = {};
    gpio_dev.name = "gpio";
    gpio_dev.vid = PDEV_VID_AMLOGIC;
    gpio_dev.pid = PDEV_PID_AMLOGIC_S912;
    gpio_dev.did = PDEV_DID_AMLOGIC_GPIO;
    gpio_dev.mmio_list = gpio_mmios;
    gpio_dev.mmio_count = countof(gpio_mmios);
    gpio_dev.irq_list = gpio_irqs;
    gpio_dev.irq_count = countof(gpio_irqs);

    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "GpioInit: pbus_protocol_device_add failed: %d\n", status);
        return status;
    }

    gpio_impl_ = ddk::GpioImplProtocolClient(parent());
    if (!gpio_impl_.is_valid()) {
        zxlogf(ERROR, "%s: device_get_protocol failed\n", __func__);
        return ZX_ERR_INTERNAL;
    }

    if (enable_gpio_test_) {
        const pbus_gpio_t gpio_test_gpios[] = {
            {
                // SYS_LED
                .gpio = S912_GPIOAO(9),
            },
            {
                // GPIO PIN
                .gpio = S912_GPIOAO(2),
            },
        };

        pbus_dev_t gpio_test_dev = {};
        gpio_test_dev.name = "vim-gpio-test";
        gpio_test_dev.vid = PDEV_VID_GENERIC;
        gpio_test_dev.pid = PDEV_PID_GENERIC;
        gpio_test_dev.did = PDEV_DID_GPIO_TEST;
        gpio_test_dev.gpio_list = gpio_test_gpios;
        gpio_test_dev.gpio_count = countof(gpio_test_gpios);

        status = pbus_.DeviceAdd(&gpio_test_dev);
        if (status != ZX_OK) {
            zxlogf(ERROR, "GpioInit could not add gpio_test_dev: %d\n", status);
            return status;
        }
    }

    return ZX_OK;
}
} //namespace vim