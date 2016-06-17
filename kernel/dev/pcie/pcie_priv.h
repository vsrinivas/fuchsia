// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <dev/pcie.h>
#include <dev/pcie_constants.h>
#include <dev/pcie_caps.h>
#include <kernel/mutex.h>
#include <list.h>

__BEGIN_CDECLS

struct pcie_bus_driver_state;

typedef struct pcie_kmap_ecam_range {
    pcie_ecam_range_t ecam;
    void*             vaddr;
} pcie_kmap_ecam_range_t;

typedef struct pcie_io_range_alloc {
    pcie_io_range_t  io;
    size_t           used;
} pcie_io_range_alloc_t;

typedef struct pcie_legacy_irq_handler_state {
    struct pcie_bus_driver_state* bus_drv;
    struct list_node              legacy_irq_list_node;
    struct list_node              device_handler_list;
    uint                          irq_id;
} pcie_legacy_irq_handler_state_t;

typedef struct pcie_bus_driver_state {
    mutex_t                         claimed_devices_lock;
    struct list_node                claimed_devices;
    pcie_bridge_state_t*            host_bridge;

    vmm_aspace_t*                   aspace;
    pcie_kmap_ecam_range_t*         ecam_windows;
    size_t                          ecam_window_count;

    pcie_io_range_alloc_t           mmio_lo;
    pcie_io_range_alloc_t           mmio_hi;
    pcie_io_range_alloc_t           pio;

    platform_legacy_irq_swizzle_t   legacy_irq_swizzle;
    spin_lock_t                     legacy_irq_handler_lock;
    mutex_t                         legacy_irq_list_lock;
    struct list_node                legacy_irq_list;

    platform_alloc_msi_block_t      alloc_msi_block;
    platform_free_msi_block_t       free_msi_block;
    platform_register_msi_handler_t register_msi_handler;
    platform_mask_unmask_msi_t      mask_unmask_msi;
} pcie_bus_driver_state_t;


/******************************************************************************
 *
 *  pcie.c
 *
 ******************************************************************************/
pcie_bus_driver_state_t* pcie_get_bus_driver_state(void);
void pcie_scan_and_start_devices(pcie_bus_driver_state_t* bus_drv);
pcie_config_t* pcie_get_config(const pcie_bus_driver_state_t* bus_drv,
                               uint64_t* cfg_phys,
                               uint bus_id,
                               uint dev_id,
                               uint func_id);

/******************************************************************************
 *
 *  pcie_caps.c
 *
 ******************************************************************************/
status_t pcie_parse_capabilities(struct pcie_device_state* dev);

/******************************************************************************
 *
 *  pcie_irqs.c
 *
 ******************************************************************************/
status_t pcie_init_device_irq_state(pcie_device_state_t* dev);
status_t pcie_init_irqs(pcie_bus_driver_state_t* drv, const pcie_init_info_t* init_info);
void     pcie_shutdown_irqs(pcie_bus_driver_state_t* drv);

__END_CDECLS
