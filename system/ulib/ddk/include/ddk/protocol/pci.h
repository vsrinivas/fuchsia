// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <ddk/driver.h>
#include <hw/pci.h>

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
    mx_status_t (*pci_wait_interrupt)(mx_handle_t handle);
    mx_handle_t (*get_config)(mx_device_t* dev, const pci_config_t** config);
    mx_status_t (*query_irq_mode_caps)(mx_device_t* dev,
                                       mx_pci_irq_mode_t mode,
                                       uint32_t* out_max_irqs);
    mx_status_t (*set_irq_mode)(mx_device_t* dev,
                                mx_pci_irq_mode_t mode,
                                uint32_t requested_irq_count);
} pci_protocol_t;

extern pci_protocol_t _pci_protocol;
