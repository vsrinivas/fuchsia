// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <assert.h>
#include <dev/pci.h>

__BEGIN_CDECLS

#define PCIE_MAX_BUSSES (256u)
#define PCIE_MAX_DEVICES_PER_BUS (32u)
#define PCIE_MAX_FUNCTIONS_PER_DEVICE (8u)
#define PCIE_MAX_FUNCTIONS_PER_BUS (PCIE_MAX_DEVICES_PER_BUS * PCIE_MAX_FUNCTIONS_PER_DEVICE)

#define PCIE_MAX_LEGACY_IRQ_PINS (4u)
#define PCIE_MAX_MSI_IRQS        (32u)
#define PCIE_MAX_MSIX_IRQS       (2048u)

#define PCIE_STANDARD_CONFIG_HDR_SIZE (64u)
#define PCIE_BASE_CONFIG_SIZE         (256u)
#define PCIE_EXTENDED_CONFIG_SIZE     (4096u)
STATIC_ASSERT(sizeof(pci_config_t) == PCIE_STANDARD_CONFIG_HDR_SIZE);

#define PCIE_ECAM_BYTE_PER_BUS (PCIE_EXTENDED_CONFIG_SIZE * PCIE_MAX_FUNCTIONS_PER_BUS)

#define PCIE_BAR_REGS_PER_BRIDGE    (2u)
#define PCIE_BAR_REGS_PER_DEVICE    (6u)
#define PCIE_MAX_BAR_REGS           (6u)

#define PCIE_INVALID_VENDOR_ID      (0xFFFF)

/**
 * The maximum possible number of standard capabilities for a PCI
 * device/function is 48.  This comes from the facts that...
 *
 * ++ There are 256 bytes in the standard configuration space.
 * ++ The first 64 bytes are used by the standard configuration header, leaving
 *    192 bytes for capabilities.
 * ++ Even though the capability header is only 2 bytes long, it must be aligned
 *    on a 4 byte boundary.  The means that one can pack (at most) 192 / 4 == 48
 *    properly aligned standard PCI capabilities.
 *
 * Similar logic may be applied to extended capabilities which must also be 4
 * byte aligned, but exist in the region after the standard configuration block.
 */
#define PCIE_CAPABILITY_ALIGNMENT  (4u)

#define PCIE_MAX_CAPABILITIES      ((PCIE_BASE_CONFIG_SIZE - sizeof(pci_config_t)) \
                                   / PCIE_CAPABILITY_ALIGNMENT)
#define PCIE_CAP_PTR_NULL          (0u)
#define PCIE_CAP_PTR_MIN_VALID     (PCIE_STANDARD_CONFIG_HDR_SIZE)
#define PCIE_CAP_PTR_MAX_VALID     (PCIE_BASE_CONFIG_SIZE - PCIE_CAPABILITY_ALIGNMENT)

#define PCIE_EXT_CAP_PTR_NULL      (0u)
#define PCIE_EXT_CAP_PTR_MIN_VALID (PCIE_BASE_CONFIG_SIZE)
#define PCIE_EXT_CAP_PTR_MAX_VALID (PCIE_EXTENDED_CONFIG_SIZE - PCIE_CAPABILITY_ALIGNMENT)
#define PCIE_MAX_EXT_CAPABILITIES  ((PCIE_EXTENDED_CONFIG_SIZE - PCIE_BASE_CONFIG_SIZE) \
                                   / PCIE_CAPABILITY_ALIGNMENT)

/*
 * PCI BAR register masks and constants
 */
#define PCI_BAR_IO_TYPE_MASK        (0x00000001)
#define PCI_BAR_IO_TYPE_MMIO        (0x00000000)
#define PCI_BAR_IO_TYPE_PIO         (0x00000001)

#define PCI_BAR_MMIO_TYPE_MASK      (0x00000006)
#define PCI_BAR_MMIO_TYPE_32BIT     (0x00000000)
#define PCI_BAR_MMIO_TYPE_64BIT     (0x00000004)

#define PCI_BAR_MMIO_PREFETCH_MASK  (0x00000008)
#define PCI_BAR_MMIO_ADDR_MASK      (0xFFFFFFF0)
#define PCI_BAR_PIO_ADDR_MASK       (0xFFFFFFFC)

/*
 * Extra bits used in the CFG command and status registers defined by PCIe.  See
 * the PCIe Base Specification, sections 7.5.1.1 and 7.5.1.2
 */
#define PCIE_CFG_COMMAND_INT_DISABLE    ((uint16_t)(1 << 10))
#define PCIE_CFG_STATUS_INT_STS         ((uint16_t)(1 << 3))

__END_CDECLS
