// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#if WITH_DEV_PCIE
#include <dev/pcie_platform.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

/**
 * Initialize the GICv2m management of MSI blocks.
 *
 * @return A status code indicating the success or failure of the initialization.
 */
zx_status_t arm_gicv2m_msi_init(void);

/**
 * Implementation of the PCIe bus driver hooks.
 *
 * @see platform_alloc_msi_block_t in dev/pcie/pcie_irqs.h
 */
zx_status_t arm_gicv2m_alloc_msi_block(uint requested_irqs,
                                       bool can_target_64bit,
                                       bool is_msix,
                                       pcie_msi_block_t* out_block);

/**
 * @see platform_free_msi_block_t in dev/pcie/pcie_irqs.h
 */
void arm_gicv2m_free_msi_block(pcie_msi_block_t* block);

/**
 * @see platform_register_msi_handler_t in dev/pcie/pcie_irqs.h
 */
void arm_gicv2m_register_msi_handler(const pcie_msi_block_t* block,
                                     uint msi_id,
                                     int_handler handler,
                                     void* ctx);

/**
 * @see platform_mask_unmask_msi_t in dev/pcie/pcie_irqs.h
 */
void arm_gicv2m_mask_unmask_msi(const pcie_msi_block_t* block,
                                uint msi_id,
                                bool mask);

__END_CDECLS
#endif // WITH_DEV_PCIE

