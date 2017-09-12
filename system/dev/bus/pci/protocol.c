// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/pci.h>

#include <assert.h>
#include <limits.h>
#include <zircon/process.h>

#include "kpci-private.h"

static zx_status_t kpci_enable_bus_master(void* ctx, bool enable) {
    kpci_device_t* device = ctx;
    return zx_pci_enable_bus_master(device->handle, enable);
}

static zx_status_t kpci_enable_pio(void* ctx, bool enable) {
    kpci_device_t* device = ctx;
    return zx_pci_enable_pio(device->handle, enable);
}

static zx_status_t kpci_reset_device(void* ctx) {
    kpci_device_t* device = ctx;
    return zx_pci_reset_device(device->handle);
}

// TODO(cja): Figure out how to handle passing PIO privileges to other
// processes in the future when PCI is moved out of the kernel into
// userspace.
static zx_status_t do_resource_bookkeeping(zx_pci_resource_t* res) {
    zx_status_t status;

    if (!res) {
        return ZX_ERR_INVALID_ARGS;
    }

    switch(res->type) {
    case PCI_RESOURCE_TYPE_PIO:
#if __x86_64__
            // x86 PIO space access requires permission in the I/O bitmap
        status = zx_mmap_device_io(get_root_resource(), res->pio_addr, res->size);
#else
        status = ZX_ERR_NOT_SUPPORTED;
#endif
        break;
    default:
        status = ZX_OK;
    }

    return status;
}

static zx_status_t pci_get_resource(void* ctx, uint32_t res_id, zx_pci_resource_t* out_res) {
    zx_status_t status = ZX_OK;

    if (!out_res || res_id >= PCI_RESOURCE_COUNT) {
        return ZX_ERR_INVALID_ARGS;
    }

    kpci_device_t* device = ctx;

    switch (res_id) {
        case PCI_RESOURCE_BAR_0:
        case PCI_RESOURCE_BAR_1:
        case PCI_RESOURCE_BAR_2:
        case PCI_RESOURCE_BAR_3:
        case PCI_RESOURCE_BAR_4:
        case PCI_RESOURCE_BAR_5:
            status = zx_pci_get_bar(device->handle, res_id, out_res);
            break;
        case PCI_RESOURCE_CONFIG:
            status = zx_pci_get_config(device->handle, out_res);
            break;
    }

    if (status != ZX_OK) {
        return status;
    }

    return do_resource_bookkeeping(out_res);
}

// Sanity check the resource enum
static_assert(PCI_RESOURCE_BAR_0 == 0, "BAR 0's value is not 0");
static_assert(PCI_RESOURCE_BAR_5 == 5, "BAR 5's value is not 5");
static_assert(PCI_RESOURCE_CONFIG > PCI_RESOURCE_BAR_5, "resource order in the enum is wrong");

/* Get a resource from the pci bus driver and map for the driver. */
static zx_status_t kpci_map_resource(void* ctx,
                                    uint32_t res_id,
                                    zx_cache_policy_t cache_policy,
                                    void** vaddr,
                                    size_t* size,
                                    zx_handle_t* out_handle) {
    if (!vaddr || !size || !out_handle) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_pci_resource_t resource;
    zx_status_t status = pci_get_resource(ctx, res_id, &resource);
    if (status != ZX_OK) {
        return status;
    }

    // TODO(cja): PIO may be mappable on non-x86 architectures
    if (resource.type == PCI_RESOURCE_TYPE_PIO) {
        return ZX_ERR_WRONG_TYPE;
    }

    uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_MAP_RANGE;
    if (res_id <= PCI_RESOURCE_BAR_5) {
        // Writes to bar resources are allowed.
        map_flags |= ZX_VM_FLAG_PERM_WRITE;

        // Bar cache policy can be controlled by the driver.
        status = zx_vmo_set_cache_policy(resource.mmio_handle, cache_policy);
        if (status != ZX_OK) {
            zx_handle_close(resource.mmio_handle);
            return status;
        }
    }

    // Map the config/bar passed in. Mappings require PAGE_SIZE alignment for
    // both base and size
    void* vaddr_tmp;
    status = zx_vmar_map(zx_vmar_root_self(), 0, resource.mmio_handle, 0,
                            ROUNDUP(resource.size, PAGE_SIZE),
                            map_flags, (uintptr_t*)&vaddr_tmp);

    if (status != ZX_OK) {
        zx_handle_close(resource.mmio_handle);
        return status;
    }

    *size = resource.size;
    *out_handle = resource.mmio_handle;
    *vaddr = vaddr_tmp;

    return status;
}

static zx_status_t kpci_map_interrupt(void* ctx, int which_irq, zx_handle_t* out_handle) {
    zx_status_t status = ZX_OK;

    if (!out_handle) {
        return ZX_ERR_INVALID_ARGS;
    }

    kpci_device_t* device = ctx;
    if (device->handle == ZX_HANDLE_INVALID) {
        return ZX_ERR_BAD_HANDLE;
    }

    status = zx_pci_map_interrupt(device->handle, which_irq, out_handle);
    if (status != ZX_OK) {
        *out_handle = ZX_HANDLE_INVALID;
        return status;
    }

    return ZX_OK;
}

static zx_status_t kpci_query_irq_mode_caps(void* ctx,
                                           zx_pci_irq_mode_t mode,
                                           uint32_t* out_max_irqs) {
    kpci_device_t* device = ctx;
    return zx_pci_query_irq_mode_caps(device->handle, mode, out_max_irqs);
}

static zx_status_t kpci_set_irq_mode(void* ctx, zx_pci_irq_mode_t mode,
                                    uint32_t requested_irq_count) {
    kpci_device_t* device = ctx;
    return zx_pci_set_irq_mode(device->handle, mode, requested_irq_count);
}

static zx_status_t kpci_get_device_info(void* ctx, zx_pcie_device_info_t* out_info) {
    if (out_info == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    kpci_device_t* device = ctx;
    *out_info = device->info;
    return ZX_OK;
}

static pci_protocol_ops_t _pci_protocol = {
    .enable_bus_master = kpci_enable_bus_master,
    .enable_pio = kpci_enable_pio,
    .reset_device = kpci_reset_device,
    .map_resource = kpci_map_resource,
    .map_interrupt = kpci_map_interrupt,
    .query_irq_mode_caps = kpci_query_irq_mode_caps,
    .set_irq_mode = kpci_set_irq_mode,
    .get_device_info = kpci_get_device_info,
};
