// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/process.h>
#include <zircon/syscalls/resource.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "platform-bus.h"

void platform_release_resources(platform_resources_t* resources) {
    for (uint32_t i = 0; i < resources->mmio_count; i++) {
        zx_handle_close(resources->mmios[i].resource);
    }
    for (uint32_t i = 0; i < resources->irq_count; i++) {
        zx_handle_close(resources->irqs[i].resource);
    }
}

zx_status_t platform_map_mmio(platform_resources_t* resources, uint32_t index,
                              uint32_t cache_policy, void** vaddr, size_t* size,
                              zx_handle_t* out_handle) {
    if (index >= resources->mmio_count) {
        return ZX_ERR_INVALID_ARGS;
    }

    platform_mmio_t* mmio = &resources->mmios[index];
    zx_handle_t vmo_handle;
    zx_status_t status = zx_vmo_create_physical(mmio->resource, mmio->base, mmio->length,
                                                &vmo_handle);
    if (status != ZX_OK) {
        printf("platform_dev_map_mmio: zx_vmo_create_physical failed %d\n", status);
        return status;
    }

    size_t vmo_size;
    status = zx_vmo_get_size(vmo_handle, &vmo_size);
    if (status != ZX_OK) {
        printf("platform_dev_map_mmio: zx_vmo_get_size failed %d\n", status);
        goto fail;
    }

    status = zx_vmo_set_cache_policy(vmo_handle, cache_policy);
    if (status != ZX_OK) {
        printf("platform_dev_map_mmio: zx_vmo_set_cache_policy failed %d\n", status);
        goto fail;
    }

    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_handle, 0, vmo_size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                         (uintptr_t*)vaddr);
    if (status != ZX_OK) {
        printf("platform_dev_map_mmio: zx_vmar_map failed %d\n", status);
        goto fail;
    }

    *size = vmo_size;
    *out_handle = vmo_handle;
    return ZX_OK;

fail:
    zx_handle_close(vmo_handle);
    return status;
}

zx_status_t platform_map_interrupt(platform_resources_t* resources, uint32_t index,
                                   zx_handle_t* out_handle) {
    if (index >= resources->irq_count || !out_handle) {
        return ZX_ERR_INVALID_ARGS;
    }
    platform_irq_t* irq = &resources->irqs[index];

    return zx_interrupt_create(irq->resource, irq->irq, ZX_FLAG_REMAP_IRQ, out_handle);
}

static zx_status_t platform_add_mmios(platform_bus_t* bus, mdi_node_ref_t* list_node,
                                      platform_resources_t* resources) {
    mdi_node_ref_t node;
    platform_mmio_t* mmios = resources->mmios;

    mdi_each_child(list_node, &node) {
        uint64_t base = 0;
        uint64_t length = 0;
        mdi_node_ref_t  child;
        mdi_each_child(&node, &child) {
            switch (mdi_id(&child)) {
            case MDI_BASE_PHYS:
                mdi_node_uint64(&child, &base);
                break;
            case MDI_LENGTH:
                mdi_node_uint64(&child, &length);
                break;
            }
        }

        if (!base || !length) {
            printf("platform_add_mmios: missing base or length\n");
            return ZX_ERR_INVALID_ARGS;
        }

        mmios->base = base;
        mmios->length = length;
        zx_status_t status = zx_resource_create(bus->resource, ZX_RSRC_KIND_MMIO, base,
                                                base + length - 1, &mmios->resource);
        if (status != ZX_OK) {
            printf("platform_add_mmios: zx_resource_create failed %d\n", status);
            return status;
        }
        mmios++;
    }

    return ZX_OK;
}

static zx_status_t platform_add_irqs(platform_bus_t* bus, mdi_node_ref_t* array_node,
                                     platform_resources_t* resources) {
    uint32_t count = mdi_array_length(array_node);
    platform_irq_t* irqs = resources->irqs;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t irq;
        zx_status_t status = mdi_array_uint32(array_node, i, &irq);
        if (status != ZX_OK) {
            return status;
        }
        irqs->irq = irq;
        status = zx_resource_create(bus->resource, ZX_RSRC_KIND_IRQ, irq, irq, &irqs->resource);
        if (status != ZX_OK) {
            printf("platform_add_irqs: zx_resource_create failed %d\n", status);
            return status;
        }
        irqs++;
    }

    return ZX_OK;
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

zx_status_t platform_add_resources(platform_bus_t* bus, platform_resources_t* resources,
                                   mdi_node_ref_t* node) {
    mdi_node_ref_t child;
    zx_status_t status;

    mdi_each_child(node, &child) {
        switch (mdi_id(&child)) {
        case MDI_PLATFORM_MMIOS:
            if ((status = platform_add_mmios(bus, &child, resources)) != ZX_OK) {
                return status;
            }
            break;
        case MDI_PLATFORM_IRQS:
            if ((status = platform_add_irqs(bus, &child, resources)) != ZX_OK) {
                return status;
            }
            break;
        }
    }

    return ZX_OK;
}
