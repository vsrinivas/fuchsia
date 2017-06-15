// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <hw/pci.h>
#include <magenta/compiler.h>
#include <magenta/syscalls/pci.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

/**
 * protocols/pci.h - PCI protocol definitions
 *
 * The PCI host driver publishes mx_device_t's with its config set to a pci_device_config_t.
 */

enum pci_resource_ids {
    PCI_RESOURCE_BAR_0 = 0,
    PCI_RESOURCE_BAR_1,
    PCI_RESOURCE_BAR_2,
    PCI_RESOURCE_BAR_3,
    PCI_RESOURCE_BAR_4,
    PCI_RESOURCE_BAR_5,
    PCI_RESOURCE_CONFIG,
    PCI_RESOURCE_COUNT,
};

typedef struct pci_protocol_ops {
    mx_status_t (*claim_device)(void* ctx);
    mx_status_t (*map_resource)(void* ctx, uint32_t res_id, uint32_t cache_policy,
                                void** vaddr, size_t* size, mx_handle_t* out_handle);
    mx_status_t (*enable_bus_master)(void* ctx, bool enable);
    mx_status_t (*enable_pio)(void* ctx, bool enable);
    mx_status_t (*reset_device)(void* ctx);
    mx_status_t (*map_interrupt)(void* ctx, int which_irq, mx_handle_t* out_handle);
    mx_status_t (*query_irq_mode_caps)(void* ctx, mx_pci_irq_mode_t mode,
                                       uint32_t* out_max_irqs);
    mx_status_t (*set_irq_mode)(void* ctx, mx_pci_irq_mode_t mode,
                                uint32_t requested_irq_count);
    mx_status_t (*get_device_info)(void* ctx, mx_pcie_device_info_t* out_info);
} pci_protocol_ops_t;

typedef struct pci_protocol {
    pci_protocol_ops_t* ops;
    void* ctx;
} pci_protocol_t;

__END_CDECLS;
