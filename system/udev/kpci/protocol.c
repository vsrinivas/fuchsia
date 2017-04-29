// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/pci.h>

#include <assert.h>
#include <magenta/syscalls.h>

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

static pci_protocol_t _pci_protocol = {
    .claim_device = pci_claim_device,
    .enable_bus_master = pci_enable_bus_master,
    .enable_pio = pci_enable_pio,
    .reset_device = pci_reset_device,
    .map_mmio = pci_map_mmio,
    .map_interrupt = pci_map_interrupt,
    .get_config = pci_get_config,
    .query_irq_mode_caps = pci_query_irq_mode_caps,
    .set_irq_mode = pci_set_irq_mode,
};
