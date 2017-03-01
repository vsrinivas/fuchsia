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

static mx_status_t pci_map_mmio(mx_device_t* dev,
                                uint32_t bar_num,
                                mx_cache_policy_t cache_policy,
                                void** vaddr,
                                uint64_t* size,
                                mx_handle_t* out_handle) {
    mx_status_t status = NO_ERROR;
    mx_handle_t mmio_handle = MX_HANDLE_INVALID;
    kpci_device_t* device = NULL;
    uintptr_t vaddr_tmp;

    if (!dev || !out_handle || !vaddr || (bar_num > PCI_MAX_BAR_COUNT - 1)) {
        return ERR_INVALID_ARGS;
    }

    device = dev->ctx;
    if (device->handle == MX_HANDLE_INVALID) {
        return ERR_BAD_HANDLE;
    }

    status = mx_pci_map_mmio(device->handle, bar_num, cache_policy, &mmio_handle);
    if (status != NO_ERROR) {
        return status;
    }

    status = mx_io_mapping_get_info(mmio_handle, &vaddr_tmp, size);
    if (status != NO_ERROR) {
        mx_handle_close(mmio_handle);
        return status;
    }

    *vaddr = (void*)(vaddr_tmp);
    *out_handle = mmio_handle;
    return NO_ERROR;
}

static mx_status_t pci_map_resource(mx_device_t* dev,
                                mx_pci_resource_t *resource,
                                mx_cache_policy_t cache_policy,
                                void** vaddr) {
    mx_status_t status = NO_ERROR;
    uint32_t map_flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_MAP_RANGE;

    if (!dev || !resource | !vaddr) {
        return ERR_INVALID_ARGS;
    }

    // TODO(cja): PIO may be mappable on non-x86 architectures
    if (resource->type == PCI_RESOURCE_TYPE_PIO) {
        return ERR_NOT_SUPPORTED;
    }

    // Set a policy if one hasn't already been set by the PCI bus driver
    status = mx_vmo_set_cache_policy(resource->mmio_handle, cache_policy);
    if (status != NO_ERROR) {
        return status;
    }

    // Check if the handle has write permissions and pass it on to the mapping
    // if necessary. Config is read-only, BARs are full access.
    mx_info_handle_basic_t info;
    status = mx_object_get_info(resource->mmio_handle, MX_INFO_HANDLE_BASIC, &info,
            sizeof(info), NULL, NULL);
    if (status != NO_ERROR) {
        return status;
    }

    if (info.rights & MX_RIGHT_WRITE) {
        map_flags |= MX_VM_FLAG_PERM_WRITE;
    }

    // Map the config/bar passed in. Mappings require PAGE_SIZE alignment for
    // both base and size
    status = mx_vmar_map(mx_vmar_root_self(), 0, resource->mmio_handle, 0,
                            ROUNDUP(resource->size, PAGE_SIZE),
                            map_flags, (uintptr_t*)vaddr);
    if (status != NO_ERROR) {
        *vaddr = NULL;
    }

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

static mx_status_t pci_get_bar(mx_device_t* dev, uint32_t bar_num, mx_pci_resource_t* out_bar) {
    mx_status_t status = NO_ERROR;

    if (!dev || !out_bar) {
        return ERR_INVALID_ARGS;
    }

    kpci_device_t* device = dev->ctx;
    if (device->handle == MX_HANDLE_INVALID) {
        return ERR_BAD_HANDLE;
    }

    status = mx_pci_get_bar(device->handle, bar_num, out_bar);
    if (status != NO_ERROR) {
        return status;
    }

    return do_resource_bookkeeping(out_bar);
}

static mx_status_t pci_get_config_ex(mx_device_t* dev, mx_pci_resource_t* out_config) {
    if (!dev || !out_config) {
        return ERR_INVALID_ARGS;
    }

    kpci_device_t* device = dev->ctx;
    if (device->handle == MX_HANDLE_INVALID) {
        return ERR_BAD_HANDLE;
    }

    mx_status_t status = mx_pci_get_config(device->handle, out_config);
    if (status != NO_ERROR) {
        return status;
    }

    return do_resource_bookkeeping(out_config);
}

static mx_status_t pci_get_config(mx_device_t* dev,
        const pci_config_t** config,
        mx_handle_t* out_handle) {
    mx_handle_t cfg_handle;
    mx_status_t status = NO_ERROR;
    struct kpci_device* device;
    uintptr_t vaddr = 0;
    uint64_t size;

    if (!dev || !out_handle || !config) {
        return ERR_INVALID_ARGS;
    }

    device = dev->ctx;
    if (device->handle == MX_HANDLE_INVALID) {
        return ERR_BAD_HANDLE;
    }

    status = mx_pci_map_config(device->handle, &cfg_handle);
    if (status != NO_ERROR) {
        return status;
    }

    status = mx_io_mapping_get_info(cfg_handle, &vaddr, &size);
    if (status != NO_ERROR) {
        mx_handle_close(cfg_handle);
        *config = NULL;
        return status;
    }

    *config = (const pci_config_t*)vaddr;
    *out_handle = cfg_handle;
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
    .map_mmio = pci_map_mmio,
    .map_resource = pci_map_resource,
    .map_interrupt = pci_map_interrupt,
    .get_config = pci_get_config,
    .get_config_ex = pci_get_config_ex,
    .get_bar = pci_get_bar,
    .query_irq_mode_caps = pci_query_irq_mode_caps,
    .set_irq_mode = pci_set_irq_mode,
    .get_device_info = pci_get_device_info,
};
