// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s912/s912-hw.h>
#include "vim.h"

static const pbus_mmio_t mailbox_mmios[] = {
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
static const pbus_irq_t mailbox_irqs[] = {
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

static const pbus_dev_t mailbox_dev = {
    .name = "mailbox",
    .vid = PDEV_VID_KHADAS,
    .pid = PDEV_PID_VIM2,
    .did = PDEV_DID_AMLOGIC_MAILBOX,
    .mmios = mailbox_mmios,
    .mmio_count = countof(mailbox_mmios),
    .irqs = mailbox_irqs,
    .irq_count = countof(mailbox_irqs),
};

static const pbus_dev_t scpi_dev = {
    .name = "scpi",
    .vid = PDEV_VID_KHADAS,
    .pid = PDEV_PID_VIM2,
    .did = PDEV_DID_AMLOGIC_SCPI,
};

zx_status_t vim2_mailbox_init(vim_bus_t* bus) {
    zx_status_t status = pbus_device_add(&bus->pbus, &mailbox_dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim2_mailbox_init: pbus_device_add Mailbox failed: %d\n", status);
        return status;
    }

    status = pbus_device_add(&bus->pbus, &scpi_dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim2_mailbox_init: pbus_device_add SCPI failed: %d\n", status);
        return status;
    }

    return ZX_OK;
}