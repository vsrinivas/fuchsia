// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>

#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include <limits.h>

#include "aml.h"

// uncomment to disable LED blinky test
#define GPIO_TEST 1

static const pbus_mmio_t gpio_mmios[] = {
    {
        .base = S905D2_GPIO_BASE,
        .length = S905D2_GPIO_LENGTH,
    },
    {
        .base = S905D2_GPIO_A0_BASE,
        .length = S905D2_GPIO_AO_LENGTH,
    },
};

static const pbus_irq_t gpio_irqs[] = {
    {
        .irq = S905D2_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_GPIO_IRQ_2,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_GPIO_IRQ_3,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_GPIO_IRQ_4,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_GPIO_IRQ_5,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_GPIO_IRQ_6,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_GPIO_IRQ_7,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    /*
    {
        .irq = S905D2_A0_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_A0_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    */
};

static pbus_dev_t gpio_dev = {
    .name = "gpio",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .did = PDEV_DID_AMLOGIC_GPIO,
    .mmios = gpio_mmios,
    .mmio_count = countof(gpio_mmios),
    .irqs = gpio_irqs,
    .irq_count = countof(gpio_irqs),
};

zx_status_t aml_gpio_init(aml_bus_t* bus) {
    zx_status_t status = pbus_device_add(&bus->pbus, &gpio_dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_init: pbus_device_add failed: %d\n", status);
        return status;
    }

    status = pbus_wait_protocol(&bus->pbus, ZX_PROTOCOL_GPIO);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_init: pbus_wait_protocol failed: %d\n", status);
        return status;
    }

    status = device_get_protocol(bus->parent, ZX_PROTOCOL_GPIO, &bus->gpio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_init: device_get_protocol failed: %d\n", status);
        return status;
    }

#if GPIO_TEST
    const pbus_gpio_t gpio_test_gpios[] = {
        {
            // SYS_LED
            .gpio = S905D2_GPIOAO(11),
        },
    };

    const pbus_dev_t gpio_test_dev = {
        .name = "aml-gpio-test",
        .vid = PDEV_VID_GENERIC,
        .pid = PDEV_PID_GENERIC,
        .did = PDEV_DID_GPIO_TEST,
        .gpios = gpio_test_gpios,
        .gpio_count = countof(gpio_test_gpios),
    };

    if ((status = pbus_device_add(&bus->pbus, &gpio_test_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_init could not add gpio_test_dev: %d\n", status);
        return status;
    }
#endif

    return ZX_OK;
}
