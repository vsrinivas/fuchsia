// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/platform-devices.h>
#include <stdio.h>

#include "hi3660-bus.h"

// TODO(voydanoff) Move hard coded values to a header file
// TODO(voydanoff) Assign IDs to the mmios and irqs

static const pbus_mmio_t dwc3_mmios[] = {
    {
        .base = 0xFF100000, // USB3OTG
        .length = 0x100000,
    },
};

static const pbus_irq_t dwc3_irqs[] = {
    {
        .irq = 191,
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
        .base = 0xFF100000, // USB3OTG
        .length = 0x100000,
    },
};

static const pbus_irq_t xhci_irqs[] = {
    {
        .irq = 191,
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
        .base = 0xE82C0000, // G3D
        .length = 0x4000,
    },
};

static const pbus_irq_t mali_irqs[] = {
    {
        .irq = 290, // JOB
    },
    {
        .irq = 291, // MMU
    },
    {
        .irq = 292, // GPU
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

zx_status_t hi3360_add_devices(hi3660_bus_t* bus) {
    zx_status_t status;

    if ((status = pbus_device_add(&bus->pbus, &dwc3_dev, 0)) != ZX_OK) {
        dprintf(ERROR, "hi3360_add_devices could not add dwc3_dev: %d\n", status);
        return status;
    }
    // xhci_dev is enabled/disabled dynamically, so don't enable it here
    if ((status = pbus_device_add(&bus->pbus, &xhci_dev, PDEV_ADD_DISABLED)) != ZX_OK) {
        dprintf(ERROR, "hi3360_add_devices could not add xhci_dev: %d\n", status);
        return status;
    }
    if ((status = pbus_device_add(&bus->pbus, &mali_dev, 0)) != ZX_OK) {
        dprintf(ERROR, "hi3360_add_devices could not add mali_dev: %d\n", status);
        return status;
    }

    return ZX_OK;
}
