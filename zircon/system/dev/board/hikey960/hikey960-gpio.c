// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <soc/hi3660/hi3660-hw.h>
#include <soc/hi3660/hi3660-pinmux.h>
#include <soc/hi3660/hi3660-regs.h>

#include <limits.h>

#include "hikey960.h"

static const pbus_mmio_t gpio_mmios[] = {
    {
        .base = MMIO_GPIO0_BASE,
        .length = PAGE_SIZE * 18,
    },
    {
        .base = MMIO_GPIO18_BASE,
        .length = PAGE_SIZE * 2,
    },
    {
        .base = MMIO_GPIO20_BASE,
        .length = PAGE_SIZE * 2,
    },
    {
        .base = MMIO_GPIO22_BASE,
        .length = PAGE_SIZE * 6,
    },
    {
        .base = MMIO_GPIO28_BASE,
        .length = PAGE_SIZE * 1,
    },
};

static const pbus_irq_t gpio_irqs[] = {
    {
        .irq = IRQ_GPIO0_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO1_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO2_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO3_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO4_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO5_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO6_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO7_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO8_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO9_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO10_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO11_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO12_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO13_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO14_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO15_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO16_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO17_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO18_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO19_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO20_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO21_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO22_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO23_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO24_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO25_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO26_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_GPIO27_INTR1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static const pbus_dev_t hikey960_gpio_dev = {
    .name = "hi3660-gpio",
    .vid = PDEV_VID_96BOARDS,
    .did = PDEV_DID_HI3660_GPIO,
    .mmio_list = gpio_mmios,
    .mmio_count = countof(gpio_mmios),
    .irq_list = gpio_irqs,
    .irq_count = countof(gpio_irqs),
};

zx_status_t hikey960_gpio_init(hikey960_t* hikey) {
zxlogf(INFO, "pbus_protocol_device_add\n");
    zx_status_t status = pbus_protocol_device_add(&hikey->pbus, ZX_PROTOCOL_GPIO_IMPL,
                                                  &hikey960_gpio_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hikey960_gpio_init: pbus_protocol_device_add failed: %d\n", status);
    }
    return status;
}
