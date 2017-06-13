// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/sdhci.h>

#include <hw/sdhci.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <unistd.h>

typedef struct pci_sdhci_device {
    mx_device_t* mxdev;
    pci_protocol_t pci;

    volatile sdhci_regs_t* regs;
    uint64_t regs_size;
    mx_handle_t regs_handle;
} pci_sdhci_device_t;

static mx_handle_t pci_sdhci_get_interrupt(void* ctx) {
    pci_sdhci_device_t* dev = ctx;
    // select irq mode
    mx_status_t status = dev->pci.ops->set_irq_mode(dev->pci.ctx, MX_PCIE_IRQ_MODE_MSI, 1);
    if (status < 0) {
        status = dev->pci.ops->set_irq_mode(dev->pci.ctx, MX_PCIE_IRQ_MODE_LEGACY, 1);
        if (status < 0) {
            printf("pci-sdhci: error %d setting irq mode\n", status);
            return status;
        }
        printf("pci-sdhci: selected legacy irq mode\n");
    }
    mx_handle_t irq_handle = MX_HANDLE_INVALID;
    // get irq handle
    status = dev->pci.ops->map_interrupt(dev->pci.ctx, 0, &irq_handle);
    if (status != MX_OK) {
        printf("pci-sdhci: error %d getting irq handle\n", status);
        return status;
    } else {
        return irq_handle;
    }
}

static mx_status_t pci_sdhci_get_mmio(void* ctx, volatile sdhci_regs_t** out) {
    pci_sdhci_device_t* dev = ctx;
    if (dev->regs == NULL) {
        mx_status_t status = dev->pci.ops->map_resource(dev->pci.ctx, PCI_RESOURCE_BAR_0,
                MX_CACHE_POLICY_UNCACHED_DEVICE, (void**)&dev->regs,
                &dev->regs_size, &dev->regs_handle);
        if (status != MX_OK) {
            printf("pci-sdhci: error %d mapping register window\n", status);
            return status;
        }
    }
    *out = dev->regs;
    return MX_OK;
}

static uint32_t pci_sdhci_get_base_clock(void* ctx) {
    return 0;
}

static mx_paddr_t pci_sdhci_get_dma_offset(void* ctx) {
    return 0;
}

static uint64_t pci_sdhci_get_quirks(void* ctx) {
    return 0;
}

static void pci_sdhci_hw_reset(void* ctx) {
    pci_sdhci_device_t* dev = ctx;
    if (!dev->regs) {
        return;
    }
    uint32_t val = dev->regs->ctrl0;
    val |= SDHCI_EMMC_HW_RESET;
    dev->regs->ctrl0 = val;
    // minimum is 1us but wait 9us for good measure
    mx_nanosleep(mx_deadline_after(MX_USEC(9)));
    val &= ~SDHCI_EMMC_HW_RESET;
    dev->regs->ctrl0 = val;
    // minimum is 200us but wait 300us for good measure
    mx_nanosleep(mx_deadline_after(MX_USEC(300)));
}

static sdhci_protocol_ops_t pci_sdhci_sdhci_proto = {
    .get_interrupt = pci_sdhci_get_interrupt,
    .get_mmio = pci_sdhci_get_mmio,
    .get_base_clock = pci_sdhci_get_base_clock,
    .get_dma_offset = pci_sdhci_get_dma_offset,
    .get_quirks = pci_sdhci_get_quirks,
    .hw_reset = pci_sdhci_hw_reset,
};

static void pci_sdhci_unbind(void* ctx) {
    pci_sdhci_device_t* dev = ctx;
    device_remove(dev->mxdev);
}

static void pci_sdhci_release(void* ctx) {
    pci_sdhci_device_t* dev = ctx;
    if (dev->regs != NULL) {
        mx_handle_close(dev->regs_handle);
    }
    free(dev);
}

static mx_protocol_device_t pci_sdhci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = pci_sdhci_unbind,
    .release = pci_sdhci_release,
};

static mx_status_t pci_sdhci_bind(void* ctx, mx_device_t* parent, void** cookie) {
    printf("pci-sdhci: bind\n");
    pci_sdhci_device_t* dev = calloc(1, sizeof(pci_sdhci_device_t));
    if (!dev) {
        printf("pci-sdhci: out of memory\n");
        return MX_ERR_NO_MEMORY;
    }

    mx_status_t status = MX_OK;
    if (device_get_protocol(parent, MX_PROTOCOL_PCI, (void*)&dev->pci)) {
        status = MX_ERR_NOT_SUPPORTED;
        goto fail;
    }

    status = dev->pci.ops->enable_bus_master(dev->pci.ctx, true);
    if (status < 0) {
        printf("pci-sdhci: error %d in enable bus master\n", status);
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "pci-sdhci",
        .ctx = dev,
        .ops = &pci_sdhci_device_proto,
        .proto_id = MX_PROTOCOL_SDHCI,
        .proto_ops = &pci_sdhci_sdhci_proto,
    };

    status = device_add(parent, &args, &dev->mxdev);
    if (status != MX_OK) {
        goto fail;
    }

    return MX_OK;
fail:
    free(dev);
    return status;
}

static mx_driver_ops_t pci_sdhci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = pci_sdhci_bind,
};

// clang-format off
MAGENTA_DRIVER_BEGIN(pci_sdhci, pci_sdhci_driver_ops, "magenta", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x08),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x05),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x01),
MAGENTA_DRIVER_END(pci_sdhci)
