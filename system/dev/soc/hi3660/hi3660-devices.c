// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <stdio.h>

#include "hi3660-bus.h"
#include "hi3660-hw.h"
#include "hikey960-hw.h"

//#define GPIO_TEST 1

static const pbus_mmio_t dwc3_mmios[] = {
    {
        .base = MMIO_USB3OTG_BASE,
        .length = MMIO_USB3OTG_LENGTH,
    },
};

static const pbus_irq_t dwc3_irqs[] = {
    {
        .irq = IRQ_USB3,
    },
};

static const pbus_dev_t dwc3_dev = {
    .name = "dwc3",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_USB_DWC3,
    .mmios = dwc3_mmios,
    .mmio_count = countof(dwc3_mmios),
    .irqs = dwc3_irqs,
    .irq_count = countof(dwc3_irqs),
};

static const pbus_mmio_t xhci_mmios[] = {
    {
        .base = MMIO_USB3OTG_BASE,
        .length = MMIO_USB3OTG_LENGTH,
    },
};

static const pbus_irq_t xhci_irqs[] = {
    {
        .irq = IRQ_USB3,
    },
};

static const pbus_dev_t xhci_dev = {
    .name = "dwc3-xhci",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_USB_XHCI,
    .mmios = xhci_mmios,
    .mmio_count = countof(xhci_mmios),
    .irqs = xhci_irqs,
    .irq_count = countof(xhci_irqs),
};

static const pbus_mmio_t mali_mmios[] = {
    {
        .base = MMIO_G3D_BASE,
        .length = MMIO_G3D_LENGTH,
    },
};

static const pbus_irq_t mali_irqs[] = {
    {
        .irq = IRQ_G3D_JOB,
    },
    {
        .irq = IRQ_G3D_MMU,
    },
    {
        .irq = IRQ_G3D_GPU,
    },
};

static const pbus_dev_t mali_dev = {
    .name = "mali",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_ARM_MALI,
    .mmios = mali_mmios,
    .mmio_count = countof(mali_mmios),
    .irqs = mali_irqs,
    .irq_count = countof(mali_irqs),
};

#if GPIO_TEST
static const pbus_gpio_t gpio_test_gpios[] = {
    {
        .gpio = GPIO_USER_LED1,
    },
    {
        .gpio = GPIO_USER_LED2,
    },
    {
        .gpio = GPIO_USER_LED3,
    },
    {
        .gpio = GPIO_USER_LED4,
    },
};

static const pbus_dev_t gpio_test_dev = {
    .name = "gpio-test",
    .vid = PDEV_VID_HI_SILICON,
    .pid = PDEV_PID_HI3660,
    .did = PDEV_DID_HI3660_GPIO_TEST,
    .gpios = gpio_test_gpios,
    .gpio_count = countof(gpio_test_gpios),
};
#endif

zx_status_t hi3360_add_devices(hi3660_bus_t* bus) {
    zx_status_t status;

    if ((status = pbus_device_add(&bus->pbus, &dwc3_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "hi3360_add_devices could not add dwc3_dev: %d\n", status);
        return status;
    }
    // xhci_dev is enabled/disabled dynamically, so don't enable it here
    if ((status = pbus_device_add(&bus->pbus, &xhci_dev, PDEV_ADD_DISABLED)) != ZX_OK) {
        zxlogf(ERROR, "hi3360_add_devices could not add xhci_dev: %d\n", status);
        return status;
    }
    if ((status = pbus_device_add(&bus->pbus, &mali_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "hi3360_add_devices could not add mali_dev: %d\n", status);
        return status;
    }

#if GPIO_TEST
    if ((status = pbus_device_add(&bus->pbus, &gpio_test_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "hi3360_add_devices could not add gpio_test_dev: %d\n", status);
        return status;
    }
#endif

    return ZX_OK;
}
