// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>
#include "vim.h"

// Fan Control GPIO regions
static const pbus_mmio_t fanctl_mmios[] = {
    // Mailbox
    {
        .base = S912_HIU_MAILBOX_BASE,
        .length = S912_HIU_MAILBOX_LENGTH,
    },
    // Mailbox Payload
    {
        .base = S912_MAILBOX_PAYLOAD_BASE,
        .length = S912_MAILBOX_PAYLOAD_LENGTH,
    },
};

// IRQ for Mailbox
static const pbus_irq_t fanctl_irqs[] = {
    {
        .irq = S912_MBOX_IRQ_RECEIV0,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
    {
        .irq = S912_MBOX_IRQ_RECEIV1,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
    {
        .irq = S912_MBOX_IRQ_RECEIV2,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
    {
        .irq = S912_MBOX_IRQ_SEND3,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
    {
        .irq = S912_MBOX_IRQ_SEND4,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
    {
        .irq = S912_MBOX_IRQ_SEND5,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
};

static const pbus_gpio_t fanctl_gpios[] = {
    {
        .gpio = S912_GPIODV(14),
    },
    {
        .gpio = S912_GPIODV(15),
    }
};

static const pbus_dev_t fanctl_dev = {
    .name = "fan-ctl",
    .vid = PDEV_VID_KHADAS,
    .pid = PDEV_PID_VIM2,
    .did = PDEV_DID_AMLOGIC_FANCTL,
    .mmios = fanctl_mmios,
    .mmio_count = countof(fanctl_mmios),
    .irqs = fanctl_irqs,
    .irq_count = countof(fanctl_irqs),
    .gpios = fanctl_gpios,
    .gpio_count = countof(fanctl_gpios),
};

zx_status_t vim2_fanctl_init(vim_bus_t* bus) {
    zx_status_t status = pbus_device_add(&bus->pbus, &fanctl_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim2_fanctl_init: pbus_device_add failed: %d\n", status);
        return status;
    }
    return ZX_OK;
}