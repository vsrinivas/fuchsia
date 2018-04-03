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
    zx_device_t* zxdev;
    pci_protocol_t pci;

    volatile sdhci_regs_t* regs;
    uint64_t regs_size;
    zx_handle_t regs_handle;
    zx_handle_t bti_handle;
} pci_sdhci_device_t;

static zx_status_t pci_sdhci_get_interrupt(void* ctx, zx_handle_t* handle_out) {
    pci_sdhci_device_t* dev = ctx;
    // select irq mode
    zx_status_t status = pci_set_irq_mode(&dev->pci, ZX_PCIE_IRQ_MODE_MSI, 1);
    if (status < 0) {
        status = pci_set_irq_mode(&dev->pci, ZX_PCIE_IRQ_MODE_LEGACY, 1);
        if (status < 0) {
            printf("pci-sdhci: error %d setting irq mode\n", status);
            return status;
        }
        printf("pci-sdhci: selected legacy irq mode\n");
    }
    // get irq handle
    status = pci_map_interrupt(&dev->pci, 0, handle_out);
    if (status != ZX_OK) {
        printf("pci-sdhci: error %d getting irq handle\n", status);
        return status;
    } else {
        return ZX_OK;
    }
}

static zx_status_t pci_sdhci_get_mmio(void* ctx, volatile sdhci_regs_t** out) {
    pci_sdhci_device_t* dev = ctx;
    if (dev->regs == NULL) {
        zx_status_t status = pci_map_bar(&dev->pci, 0u, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                (void**)&dev->regs, &dev->regs_size, &dev->regs_handle);
        if (status != ZX_OK) {
            printf("pci-sdhci: error %d mapping register window\n", status);
            return status;
        }
    }
    *out = dev->regs;
    return ZX_OK;
}

static zx_status_t pci_sdhci_get_bti(void* ctx, uint32_t index, zx_handle_t* out_handle) {
    pci_sdhci_device_t* dev = ctx;
    if (dev->bti_handle == ZX_HANDLE_INVALID) {
        zx_status_t st = pci_get_bti(&dev->pci, index, &dev->bti_handle);
        if (st != ZX_OK) {
            return st;
        }
    }
    return zx_handle_duplicate(dev->bti_handle, ZX_RIGHT_SAME_RIGHTS, out_handle);
}

static uint32_t pci_sdhci_get_base_clock(void* ctx) {
    return 0;
}

static uint64_t pci_sdhci_get_quirks(void* ctx) {
    return SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER;
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
    zx_nanosleep(zx_deadline_after(ZX_USEC(9)));
    val &= ~SDHCI_EMMC_HW_RESET;
    dev->regs->ctrl0 = val;
    // minimum is 200us but wait 300us for good measure
    zx_nanosleep(zx_deadline_after(ZX_USEC(300)));
}

static sdhci_protocol_ops_t pci_sdhci_sdhci_proto = {
    .get_interrupt = pci_sdhci_get_interrupt,
    .get_mmio = pci_sdhci_get_mmio,
    .get_bti = pci_sdhci_get_bti,
    .get_base_clock = pci_sdhci_get_base_clock,
    .get_quirks = pci_sdhci_get_quirks,
    .hw_reset = pci_sdhci_hw_reset,
};

static void pci_sdhci_unbind(void* ctx) {
    pci_sdhci_device_t* dev = ctx;
    device_remove(dev->zxdev);
}

static void pci_sdhci_release(void* ctx) {
    pci_sdhci_device_t* dev = ctx;
    if (dev->regs != NULL) {
        zx_handle_close(dev->regs_handle);
    }
    zx_handle_close(dev->bti_handle);
    free(dev);
}

static zx_protocol_device_t pci_sdhci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = pci_sdhci_unbind,
    .release = pci_sdhci_release,
};

static zx_status_t pci_sdhci_bind(void* ctx, zx_device_t* parent) {
    printf("pci-sdhci: bind\n");
    pci_sdhci_device_t* dev = calloc(1, sizeof(pci_sdhci_device_t));
    if (!dev) {
        printf("pci-sdhci: out of memory\n");
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = ZX_OK;
    if (device_get_protocol(parent, ZX_PROTOCOL_PCI, (void*)&dev->pci)) {
        status = ZX_ERR_NOT_SUPPORTED;
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
        .proto_id = ZX_PROTOCOL_SDHCI,
        .proto_ops = &pci_sdhci_sdhci_proto,
    };

    status = device_add(parent, &args, &dev->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    return ZX_OK;
fail:
    free(dev);
    return status;
}

static zx_driver_ops_t pci_sdhci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = pci_sdhci_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(pci_sdhci, pci_sdhci_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x08),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x05),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x01),
ZIRCON_DRIVER_END(pci_sdhci)
