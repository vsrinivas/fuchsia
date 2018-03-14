// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/assert.h>

#include "qemu-virt.h"

typedef struct {
    platform_bus_protocol_t pbus;
} qemu_bus_t;

static zx_status_t qemu_pci_init(void) {
    zx_status_t status;

    zx_pci_init_arg_t* arg;
    size_t arg_size = sizeof(*arg) + sizeof(arg->addr_windows[0]);   // room for one addr window
    arg = calloc(1, arg_size);
    if (!arg) return ZX_ERR_NO_MEMORY;

    status = zx_pci_add_subtract_io_range(get_root_resource(), true /* mmio */,
                                          PCIE_MMIO_BASE_PHYS, PCIE_MMIO_SIZE, true /* add */);
    if (status != ZX_OK) {
        goto fail;
    }
    status = zx_pci_add_subtract_io_range(get_root_resource(), false /* pio */,
                                          PCIE_PIO_BASE_PHYS, PCIE_PIO_SIZE, true /* add */);
    if (status != ZX_OK) {
        goto fail;
    }

    // initialize our swizzle table
    zx_pci_irq_swizzle_lut_t* lut = &arg->dev_pin_to_global_irq;
    for (unsigned dev_id = 0; dev_id < ZX_PCI_MAX_DEVICES_PER_BUS; dev_id++) {
        for (unsigned func_id = 0; func_id < ZX_PCI_MAX_FUNCTIONS_PER_DEVICE; func_id++) {
            for (unsigned pin = 0; pin < ZX_PCI_MAX_LEGACY_IRQ_PINS; pin++) {
                (*lut)[dev_id][func_id][pin] = PCIE_INT_BASE +
                                               (pin + dev_id) % ZX_PCI_MAX_LEGACY_IRQ_PINS;
            }
        }
    }
    arg->num_irqs = 0;
    arg->addr_window_count = 1;
    arg->addr_windows[0].is_mmio = true;
    arg->addr_windows[0].has_ecam = true;
    arg->addr_windows[0].base = PCIE_ECAM_BASE_PHYS;
    arg->addr_windows[0].size = PCIE_ECAM_SIZE;
    arg->addr_windows[0].bus_start = 0;
    arg->addr_windows[0].bus_end = (PCIE_ECAM_SIZE / ZX_PCI_ECAM_BYTE_PER_BUS) - 1;

    status = zx_pci_init(get_root_resource(), arg, arg_size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: error %d in zx_pci_init\n", __FUNCTION__, status);
        goto fail;
    }

fail:
    free(arg);
    return status;
}

static void qemu_bus_release(void* ctx) {
    qemu_bus_t* bus = ctx;
    free(bus);
}

static zx_protocol_device_t qemu_bus_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = qemu_bus_release,
};

static const pbus_mmio_t pl031_mmios[] = {
    {
        .base = RTC_BASE_PHYS,
        .length = RTC_SIZE,
    },
};

static const pbus_dev_t pl031_dev = {
    .name = "pl031",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_RTC_PL031,
    .mmios = pl031_mmios,
    .mmio_count = countof(pl031_mmios),
};

static zx_status_t qemu_bus_bind(void* ctx, zx_device_t* parent) {
    // we don't really need a context struct yet, but lets create one for future expansion.
    qemu_bus_t* bus = calloc(1, sizeof(qemu_bus_t));
    if (!bus) {
        return ZX_ERR_NO_MEMORY;
    }

    if (device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &bus->pbus) != ZX_OK) {
        free(bus);
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = qemu_pci_init();
    if (status != ZX_OK) {
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "qemu-bus",
        .ops = &qemu_bus_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    pbus_bti_t pci_btis[] = {
        {
            .iommu_index = 0,
            .bti_id = 0,
        },
    };

    pbus_dev_t pci_dev = {
        .name = "pci",
        .vid = PDEV_VID_GENERIC,
        .pid = PDEV_PID_GENERIC,
        .did = PDEV_DID_KPCI,
        .btis = pci_btis,
        .bti_count = countof(pci_btis),
    };

    status = pbus_device_add(&bus->pbus, &pci_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "qemu_bus_bind could not add pci_dev: %d\n", status);
    }

    status = pbus_device_add(&bus->pbus, &pl031_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "qemu_bus_bind could not add pl031: %d\n", status);
    }

    return ZX_OK;

fail:
    printf("qemu_bus_bind failed %d\n", status);
    qemu_bus_release(bus);
    return status;
}

static zx_driver_ops_t qemu_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = qemu_bus_bind,
};

ZIRCON_DRIVER_BEGIN(qemu_bus, qemu_bus_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_QEMU),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_QEMU),
ZIRCON_DRIVER_END(qemu_bus)
