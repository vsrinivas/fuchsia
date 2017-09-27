// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <zircon/process.h>
#include <zircon/syscalls/resource.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "platform-bus.h"

zx_status_t platform_map_mmio(platform_dev_t* dev, uint32_t index, uint32_t cache_policy,
                              void** vaddr, size_t* size, zx_handle_t* out_handle) {
    platform_resources_t* resources = &dev->resources;

    if (index >= resources->mmio_count) {
        return ZX_ERR_INVALID_ARGS;
    }

    platform_mmio_t* mmio = &resources->mmios[index];
    zx_handle_t vmo_handle;
    zx_status_t status = zx_vmo_create_physical(dev->bus->resource, mmio->base, mmio->length,
                                                &vmo_handle);
    if (status != ZX_OK) {
        dprintf(ERROR, "platform_dev_map_mmio: zx_vmo_create_physical failed %d\n", status);
        return status;
    }

    size_t vmo_size;
    status = zx_vmo_get_size(vmo_handle, &vmo_size);
    if (status != ZX_OK) {
        dprintf(ERROR, "platform_dev_map_mmio: zx_vmo_get_size failed %d\n", status);
        goto fail;
    }

    status = zx_vmo_set_cache_policy(vmo_handle, cache_policy);
    if (status != ZX_OK) {
        dprintf(ERROR, "platform_dev_map_mmio: zx_vmo_set_cache_policy failed %d\n", status);
        goto fail;
    }

    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_handle, 0, vmo_size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                         (uintptr_t*)vaddr);
    if (status != ZX_OK) {
        dprintf(ERROR, "platform_dev_map_mmio: zx_vmar_map failed %d\n", status);
        goto fail;
    }

    *size = vmo_size;
    *out_handle = vmo_handle;
    return ZX_OK;

fail:
    zx_handle_close(vmo_handle);
    return status;
}

zx_status_t platform_map_interrupt(platform_dev_t* dev, uint32_t index, zx_handle_t* out_handle) {
    platform_resources_t* resources = &dev->resources;

    if (index >= resources->irq_count || !out_handle) {
        return ZX_ERR_INVALID_ARGS;
    }
    platform_irq_t* irq = &resources->irqs[index];

    return zx_interrupt_create(dev->bus->resource, irq->irq, ZX_FLAG_REMAP_IRQ, out_handle);
}

void platform_init_resources(platform_resources_t* resources, uint32_t mmio_count,
                             uint32_t irq_count) {
    resources->mmio_count = mmio_count;
    resources->irq_count = irq_count;

    if (mmio_count > 0) {
        resources->mmios = (platform_mmio_t *)resources->extra;
    } else {
        resources->mmios = NULL;
    }
    if (irq_count > 0) {
        resources->irqs = (platform_irq_t *)&resources->extra[mmio_count * sizeof(platform_mmio_t)];
    } else {
        resources->irqs = NULL;
    }
}

zx_status_t platform_bus_add_mmios(platform_bus_t* bus, platform_resources_t* resources,
                                   const pbus_mmio_t* pbus_mmios, size_t mmio_count) {
    platform_mmio_t* mmios = resources->mmios;

    for (size_t i = 0; i < mmio_count; i++) {
        const pbus_mmio_t* pbus_mmio = pbus_mmios++;
        zx_paddr_t base = pbus_mmio->base;
        size_t length = pbus_mmio->length;

        if (!base || !length) {
            dprintf(ERROR, "platform_add_mmios: missing base or length\n");
            return ZX_ERR_INVALID_ARGS;
        }

        mmios->base = base;
        mmios->length = length;
        mmios++;
    }

    return ZX_OK;
}

zx_status_t platform_bus_add_irqs(platform_bus_t* bus, platform_resources_t* resources,
                                  const pbus_irq_t* pbus_irqs, size_t irq_count) {
    platform_irq_t* irqs = resources->irqs;

    for (size_t i = 0; i < irq_count; i++) {
        const pbus_irq_t* pbus_irq = pbus_irqs++;
        uint32_t irq = pbus_irq->irq;
        irqs->irq = irq;
        irqs++;
    }

    return ZX_OK;
}
