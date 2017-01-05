// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <assert.h>


// TODO(cja): Find C users of this header and see if we can convert to pure
//            C++ for it and use constexprs.

__BEGIN_CDECLS

/*
 * PCI access return codes
 */
#define _PCI_SUCCESSFUL             0x00
#define _PCI_FUNC_NOT_SUPPORTED     0x81
#define _PCI_BAD_VENDOR_ID          0x83
#define _PCI_DEVICE_NOT_FOUND       0x86
#define _PCI_BAD_REGISTER_NUMBER    0x87
#define _PCI_SET_FAILED             0x88
#define _PCI_BUFFER_TOO_SMALL       0x89

/*
 * PCI configuration space offsets
 */
#define PCI_CONFIG_VENDOR_ID        0x00
#define PCI_CONFIG_DEVICE_ID        0x02
#define PCI_CONFIG_COMMAND          0x04
#define PCI_CONFIG_STATUS           0x06
#define PCI_CONFIG_REVISION_ID      0x08
#define PCI_CONFIG_CLASS_CODE       0x09
#define PCI_CONFIG_CLASS_CODE_INTR  0x09
#define PCI_CONFIG_CLASS_CODE_SUB   0x0a
#define PCI_CONFIG_CLASS_CODE_BASE  0x0b
#define PCI_CONFIG_CACHE_LINE_SIZE  0x0c
#define PCI_CONFIG_LATENCY_TIMER    0x0d
#define PCI_CONFIG_HEADER_TYPE      0x0e
#define PCI_CONFIG_BIST             0x0f
#define PCI_CONFIG_BASE_ADDRESSES   0x10
#define PCI_CONFIG_CARDBUS_CIS_PTR  0x28
#define PCI_CONFIG_SUBSYS_VENDOR_ID 0x2c
#define PCI_CONFIG_SUBSYS_ID        0x2e
#define PCI_CONFIG_EXP_ROM_ADDRESS  0x30
#define PCI_CONFIG_CAPABILITIES     0x34
#define PCI_CONFIG_INTERRUPT_LINE   0x3c
#define PCI_CONFIG_INTERRUPT_PIN    0x3d
#define PCI_CONFIG_MIN_GRANT        0x3e
#define PCI_CONFIG_MAX_LATENCY      0x3f

/*
 * PCI header type register bits
 */
#define PCI_HEADER_TYPE_MASK        0x7f
#define PCI_HEADER_TYPE_MULTI_FN    0x80

/*
 * PCI header types
 */
#define PCI_HEADER_TYPE_STANDARD    0x00
#define PCI_HEADER_TYPE_PCI_BRIDGE  0x01
#define PCI_HEADER_TYPE_CARD_BUS    0x02

/*
 * PCI command register bits
 */
#define PCI_COMMAND_IO_EN           0x0001
#define PCI_COMMAND_MEM_EN          0x0002
#define PCI_COMMAND_BUS_MASTER_EN   0x0004
#define PCI_COMMAND_SPECIAL_EN      0x0008
#define PCI_COMMAND_MEM_WR_INV_EN   0x0010
#define PCI_COMMAND_PAL_SNOOP_EN    0x0020
#define PCI_COMMAND_PERR_RESP_EN    0x0040
#define PCI_COMMAND_AD_STEP_EN      0x0080
#define PCI_COMMAND_SERR_EN         0x0100
#define PCI_COMMAND_FAST_B2B_EN     0x0200


/*
 * PCI(e) general configuration definitions
 */
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

#define PCIE_MAX_CAPABILITIES      ((PCIE_BASE_CONFIG_SIZE - PCIE_STANDARD_CONFIG_HDR_SIZE) \
                                   / PCIE_CAPABILITY_ALIGNMENT)
#define PCIE_CAP_PTR_NULL          (0u)
#define PCIE_CAP_PTR_MIN_VALID     (PCIE_STANDARD_CONFIG_HDR_SIZE)
#define PCIE_CAP_PTR_MAX_VALID     (PCIE_BASE_CONFIG_SIZE - PCIE_CAPABILITY_ALIGNMENT)
#define PCIE_CAP_PTR_ALIGNMENT     (2u)

#define PCIE_EXT_CAP_PTR_NULL      (0u)
#define PCIE_EXT_CAP_PTR_MIN_VALID (PCIE_BASE_CONFIG_SIZE)
#define PCIE_EXT_CAP_PTR_MAX_VALID (PCIE_EXTENDED_CONFIG_SIZE - PCIE_CAPABILITY_ALIGNMENT)
#define PCIE_EXT_CAP_PTR_ALIGNMENT (4u)
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

#ifdef __cplusplus
enum class PciAddrSpace { MMIO, PIO };
#if ARCH_X86
constexpr bool PCIE_HAS_IO_ADDR_SPACE = true;
constexpr uint64_t PCIE_PIO_ADDR_SPACE_MASK = 0xFFFF;
constexpr uint64_t PCIE_PIO_ADDR_SPACE_SIZE = 0x10000;
#else  // #if (defined(ARCH_X86) && ARCH_X86)
constexpr bool PCIE_HAS_IO_ADDR_SPACE = false;
constexpr uint64_t PCIE_PIO_ADDR_SPACE_MASK = 0xFFFFFFFF;
constexpr uint64_t PCIE_PIO_ADDR_SPACE_SIZE = 0x100000000;
#endif  // #if (defined(ARCH_X86) && ARCH_X86)
#endif  // __cplusplus
