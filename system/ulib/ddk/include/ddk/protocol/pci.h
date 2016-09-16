// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/syscalls-types.h>
#include <ddk/driver.h>
#include <hw/pci.h>

__BEGIN_CDECLS;

/**
 * protocols/pci.h - PCI protocol definitions
 *
 * The PCI host driver publishes mx_device_t's with its config set to a pci_device_config_t.
 */

typedef struct pci_protocol {
    mx_status_t (*claim_device)(mx_device_t* dev);
    mx_handle_t (*map_mmio)(mx_device_t* dev,
                            uint32_t bar_num,
                            mx_cache_policy_t cache_policy,
                            void** vaddr,
                            uint64_t* size);
    mx_status_t (*enable_bus_master)(mx_device_t* dev, bool enable);
    mx_status_t (*reset_device)(mx_device_t* dev);
    mx_handle_t (*map_interrupt)(mx_device_t* dev, int which_irq);
    mx_handle_t (*get_config)(mx_device_t* dev, const pci_config_t** config);
    mx_status_t (*query_irq_mode_caps)(mx_device_t* dev,
                                       mx_pci_irq_mode_t mode,
                                       uint32_t* out_max_irqs);
    mx_status_t (*set_irq_mode)(mx_device_t* dev,
                                mx_pci_irq_mode_t mode,
                                uint32_t requested_irq_count);
} pci_protocol_t;

__END_CDECLS;
