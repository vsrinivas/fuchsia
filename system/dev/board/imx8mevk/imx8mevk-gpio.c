// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>

#include <soc/imx8m/imx8m.h>
#include <soc/imx8m/imx8m-hw.h>
#include <soc/imx8m/imx8m-iomux.h>
#include <soc/imx8m/imx8m-gpio.h>
#include <limits.h>

#include "imx8mevk.h"

// uncomment to disable LED blinky test
#define GPIO_TEST 1

static const pbus_mmio_t gpio_mmios[] = {
    {
        .base = IMX8M_AIPS_GPIO1_BASE,
        .length = IMX8M_AIPS_LENGTH,
    },
    {
        .base = IMX8M_AIPS_GPIO2_BASE,
        .length = IMX8M_AIPS_LENGTH,
    },
    {
        .base = IMX8M_AIPS_GPIO3_BASE,
        .length = IMX8M_AIPS_LENGTH,
    },
    {
        .base = IMX8M_AIPS_GPIO4_BASE,
        .length = IMX8M_AIPS_LENGTH,
    },
    {
        .base = IMX8M_AIPS_GPIO5_BASE,
        .length = IMX8M_AIPS_LENGTH,
    },
    {
        .base = IMX8M_AIPS_IOMUXC_BASE,
        .length = IMX8M_AIPS_LENGTH,
    }
};

static const pbus_irq_t gpio_irqs[] = {
    {
        .irq = IMX8M_A53_INTR_GPIO1_INT_COMB_0_15,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8M_A53_INTR_GPIO1_INT_COMP_16_31,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8M_A53_INTR_GPIO2_INT_COMB_0_15,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8M_A53_INTR_GPIO2_INT_COMP_16_31,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8M_A53_INTR_GPIO3_INT_COMB_0_15,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8M_A53_INTR_GPIO3_INT_COMP_16_31,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8M_A53_INTR_GPIO4_INT_COMB_0_15,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8M_A53_INTR_GPIO4_INT_COMP_16_31,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8M_A53_INTR_GPIO5_INT_COMB_0_15,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8M_A53_INTR_GPIO5_INT_COMP_16_31,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static pbus_dev_t gpio_dev = {
    .name = "gpio",
    .vid = PDEV_VID_NXP,
    .pid = PDEV_PID_IMX8MEVK,
    .did = PDEV_DID_IMX_GPIO,
    .mmios = gpio_mmios,
    .mmio_count = countof(gpio_mmios),
    .irqs = gpio_irqs,
    .irq_count = countof(gpio_irqs),
};

zx_status_t imx8m_gpio_init(imx8mevk_bus_t* bus) {
    zx_status_t status = pbus_device_add(&bus->pbus, &gpio_dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_device_add failed %d\n", __FUNCTION__, status);
        return status;
    }

    status = pbus_wait_protocol(&bus->pbus, ZX_PROTOCOL_GPIO);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_wait_protocol failed %d\n", __FUNCTION__, status);
        return status;
    }

    status = device_get_protocol(bus->parent, ZX_PROTOCOL_GPIO, &bus->gpio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: device_get_protocol failed %d\n", __FUNCTION__, status);
        return status;
    }

#if GPIO_TEST
    const pbus_gpio_t gpio_test_gpios[] = {
        {
            // PWR_LED
            .gpio = IMX_GPIO_PIN(1, 13), // GPIO blocks nums are 1-based
        },
    };

    const pbus_dev_t gpio_test_dev = {
        .name = "imx8mevk-gpio-test",
        .vid = PDEV_VID_GENERIC,
        .pid = PDEV_PID_GENERIC,
        .did = PDEV_DID_GPIO_TEST,
        .gpios = gpio_test_gpios,
        .gpio_count = countof(gpio_test_gpios),
    };
    if ((status = pbus_device_add(&bus->pbus, &gpio_test_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "%s: Could not add gpio_test_dev %d\n", __FUNCTION__, status);
        return status;
    }
#endif

    return ZX_OK;
}