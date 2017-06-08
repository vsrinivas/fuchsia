// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/pci.h>

#include <assert.h>
#include <limits.h>
#include <magenta/process.h>

#include "kpci-private.h"

static mx_status_t pci_claim_device(mx_device_t* dev) {
    kpci_device_t* device = dev->ctx;
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_claim_device(device->handle);
}

static mx_status_t pci_enable_bus_master(mx_device_t* dev, bool enable) {
    kpci_device_t* device = dev->ctx;
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_enable_bus_master(device->handle, enable);
}

static mx_status_t pci_enable_pio(mx_device_t* dev, bool enable) {
    kpci_device_t* device = dev->ctx;
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_enable_pio(device->handle, enable);
}

static mx_status_t pci_reset_device(mx_device_t* dev) {
    kpci_device_t* device = dev->ctx;
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_reset_device(device->handle);
}

// TODO(cja): Figure out how to handle passing PIO privileges to other
// processes in the future when PCI is moved out of the kernel into
// userspace.
static mx_status_t do_resource_bookkeeping(mx_pci_resource_t* res) {
    mx_status_t status;

    if (!res) {
        return ERR_INVALID_ARGS;
    }

    switch(res->type) {
        case PCI_RESOURCE_TYPE_PIO:
#if __x86_64__
            // x86 PIO space access requires permission in the I/O bitmap
            status = mx_mmap_device_io(get_root_resource(), res->pio_addr, res->size);
#else
            status = ERR_NOT_SUPPORTED;
#endif
            break;
        default:
            status = NO_ERROR;
    }

    return status;
}

static mx_status_t pci_get_resource(mx_device_t* dev, uint32_t res_id, mx_pci_resource_t* out_res) {
    mx_status_t status = NO_ERROR;

    if (!dev || !out_res || res_id >= PCI_RESOURCE_COUNT) {
        return ERR_INVALID_ARGS;
    }

    kpci_device_t* device = dev->ctx;
    if (device->handle == MX_HANDLE_INVALID) {
        return ERR_BAD_HANDLE;
    }

    switch (res_id) {
        case PCI_RESOURCE_BAR_0:
        case PCI_RESOURCE_BAR_1:
        case PCI_RESOURCE_BAR_2:
        case PCI_RESOURCE_BAR_3:
        case PCI_RESOURCE_BAR_4:
        case PCI_RESOURCE_BAR_5:
            status = mx_pci_get_bar(device->handle, res_id, out_res);
            break;
        case PCI_RESOURCE_CONFIG:
            status = mx_pci_get_config(device->handle, out_res);;
            break;
    }

    if (status != NO_ERROR) {
        return status;
    }

    return do_resource_bookkeeping(out_res);
}

// Sanity check the resource enum
static_assert(PCI_RESOURCE_BAR_0 == 0, "BAR 0's value is not 0");
static_assert(PCI_RESOURCE_BAR_5 == 5, "BAR 5's value is not 5");
static_assert(PCI_RESOURCE_CONFIG > PCI_RESOURCE_BAR_5, "resource order in the enum is wrong");

/* Get a resource from the pci bus driver and map for the driver. */
static mx_status_t pci_map_resource(mx_device_t* dev,
                                    uint32_t res_id,
                                    mx_cache_policy_t cache_policy,
                                    void** vaddr,
                                    size_t* size,
                                    mx_handle_t* out_handle) {
    if (!dev || !vaddr || !size || !out_handle) {
        return ERR_INVALID_ARGS;
    }

    mx_pci_resource_t resource;
    mx_status_t status = pci_get_resource(dev, res_id, &resource);
    if (status != NO_ERROR) {
        return status;
    }

    // TODO(cja): PIO may be mappable on non-x86 architectures
    if (resource.type == PCI_RESOURCE_TYPE_PIO) {
        return ERR_WRONG_TYPE;
    }

    uint32_t map_flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_MAP_RANGE;
    if (res_id <= PCI_RESOURCE_BAR_5) {
        // Writes to bar resources are allowed.
        map_flags |= MX_VM_FLAG_PERM_WRITE;

        // Bar cache policy can be controlled by the driver.
        status = mx_vmo_set_cache_policy(resource.mmio_handle, cache_policy);
        if (status != NO_ERROR) {
            mx_handle_close(resource.mmio_handle);
            return status;
        }
    }

    // Map the config/bar passed in. Mappings require PAGE_SIZE alignment for
    // both base and size
    void* vaddr_tmp;
    status = mx_vmar_map(mx_vmar_root_self(), 0, resource.mmio_handle, 0,
                            ROUNDUP(resource.size, PAGE_SIZE),
                            map_flags, (uintptr_t*)&vaddr_tmp);

    if (status != NO_ERROR) {
        mx_handle_close(resource.mmio_handle);
        return status;
    }

    *size = resource.size;
    *out_handle = resource.mmio_handle;
    *vaddr = vaddr_tmp;

    return status;
}

static mx_status_t pci_map_interrupt(mx_device_t* dev, int which_irq, mx_handle_t* out_handle) {
    mx_status_t status = NO_ERROR;

    if (!dev || !out_handle) {
        return ERR_INVALID_ARGS;
    }

    kpci_device_t* device = dev->ctx;
    if (device->handle == MX_HANDLE_INVALID) {
        return ERR_BAD_HANDLE;
    }

    status = mx_pci_map_interrupt(device->handle, which_irq, out_handle);
    if (status != NO_ERROR) {
        *out_handle = MX_HANDLE_INVALID;
        return status;
    }

    return NO_ERROR;
}

static mx_status_t pci_query_irq_mode_caps(mx_device_t* dev,
                                           mx_pci_irq_mode_t mode,
                                           uint32_t* out_max_irqs) {
    kpci_device_t* device = dev->ctx;
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_query_irq_mode_caps(device->handle, mode, out_max_irqs);
}

static mx_status_t pci_set_irq_mode(mx_device_t* dev,
                                    mx_pci_irq_mode_t mode,
                                    uint32_t requested_irq_count) {
    kpci_device_t* device = dev->ctx;
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_set_irq_mode(device->handle, mode, requested_irq_count);
}

static mx_status_t pci_get_device_info(mx_device_t* dev, mx_pcie_device_info_t* out_info) {
    if ((dev == NULL) || (out_info == NULL))
        return ERR_INVALID_ARGS;

    kpci_device_t* device = dev->ctx;
    assert(device != NULL);

    *out_info = device->info;
    return NO_ERROR;
}

static pci_protocol_t _pci_protocol = {
    .claim_device = pci_claim_device,
    .enable_bus_master = pci_enable_bus_master,
    .enable_pio = pci_enable_pio,
    .reset_device = pci_reset_device,
    .map_resource = pci_map_resource,
    .map_interrupt = pci_map_interrupt,
    .query_irq_mode_caps = pci_query_irq_mode_caps,
    .set_irq_mode = pci_set_irq_mode,
    .get_device_info = pci_get_device_info,
};
