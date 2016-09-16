// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/pci.h>

#include <assert.h>
#include <magenta/syscalls.h>

#include "kpci-private.h"

static mx_status_t pci_claim_device(mx_device_t* dev) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_claim_device(device->handle);
}

static mx_status_t pci_enable_bus_master(mx_device_t* dev, bool enable) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_enable_bus_master(device->handle, enable);
}

static mx_status_t pci_reset_device(mx_device_t* dev) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_reset_device(device->handle);
}

static mx_handle_t pci_map_mmio(mx_device_t* dev,
                                uint32_t bar_num,
                                mx_cache_policy_t cache_policy,
                                void** vaddr,
                                uint64_t* size) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    assert(vaddr != NULL);
    assert(size != NULL);

    mx_handle_t mmio_handle;
    mmio_handle = mx_pci_map_mmio(device->handle, bar_num, cache_policy);
    if (mmio_handle < 0)
        return mmio_handle;

    mx_status_t status = mx_io_mapping_get_info(mmio_handle, vaddr, size);
    if (status != NO_ERROR) {
        assert(status < 0);
        mx_handle_close(mmio_handle);
        return status;
    }

    return mmio_handle;
}

static mx_handle_t pci_map_interrupt(mx_device_t* dev, int which_irq) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_map_interrupt(device->handle, which_irq);
}

static mx_handle_t pci_get_config(mx_device_t* dev, const pci_config_t** config) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    assert(config != NULL);

    mx_handle_t cfg_handle;
    cfg_handle = mx_pci_map_config(device->handle);
    if (cfg_handle < 0)
        return cfg_handle;

    void* vaddr = NULL;
    uint64_t size;

    mx_status_t status = mx_io_mapping_get_info(cfg_handle, &vaddr, &size);
    if (status != NO_ERROR) {
        assert(status < 0);
        mx_handle_close(cfg_handle);
        *config = NULL;
        return status;
    }

    *config = (const pci_config_t*)vaddr;
    return cfg_handle;
}

static mx_status_t pci_query_irq_mode_caps(mx_device_t* dev,
                                           mx_pci_irq_mode_t mode,
                                           uint32_t* out_max_irqs) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_query_irq_mode_caps(device->handle, mode, out_max_irqs);
}

static mx_status_t pci_set_irq_mode(mx_device_t* dev,
                                    mx_pci_irq_mode_t mode,
                                    uint32_t requested_irq_count) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_set_irq_mode(device->handle, mode, requested_irq_count);
}

static pci_protocol_t _pci_protocol = {
    .claim_device = pci_claim_device,
    .enable_bus_master = pci_enable_bus_master,
    .reset_device = pci_reset_device,
    .map_mmio = pci_map_mmio,
    .map_interrupt = pci_map_interrupt,
    .get_config = pci_get_config,
    .query_irq_mode_caps = pci_query_irq_mode_caps,
    .set_irq_mode = pci_set_irq_mode,
};
