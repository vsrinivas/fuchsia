// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off


// Base Address Registers are accessed in userspace via the get_bar protocol method. The
// Bar is represented via a pci_bar_t struct which contains a handle pointer to a VMO
// in the case of an MMIO bar, as well as a PIO addr/size pair for the memory region
// to access if a PIO bar. In the latter case, the protocol will acquire the appropriate
// permissions for the process to write to that PIO region on that architecture.
typedef uint32_t zx_pci_bar_types_t;
#define ZX_PCI_BAR_TYPE_UNUSED ((zx_pci_bar_types_t) 0u)
#define ZX_PCI_BAR_TYPE_MMIO ((zx_pci_bar_types_t) 1u)
#define ZX_PCI_BAR_TYPE_PIO ((zx_pci_bar_types_t) 2u)

// TODO(cja): This makes some assumptions that anything in an arch's PIO region
// is going to be defined as a base address and size. This will need to be
// updated to a per-platform structure in the event that doesn't pan out
// in the future.
typedef struct zx_pci_bar {
    uint32_t id;
    uint32_t type;
    size_t size;
    union {
        uintptr_t addr;
        zx_handle_t handle;
    };
} zx_pci_bar_t;

// Defines and structures related to zx_pci_*()
// Info returned to dev manager for PCIe devices when probing.
typedef struct zx_pcie_device_info {
    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t  base_class;
    uint8_t  sub_class;
    uint8_t  program_interface;
    uint8_t  revision_id;

    uint8_t  bus_id;
    uint8_t  dev_id;
    uint8_t  func_id;
} zx_pcie_device_info_t;

#define ZX_PCI_MAX_BUSSES (256u)
#define ZX_PCI_MAX_DEVICES_PER_BUS (32u)
#define ZX_PCI_MAX_FUNCTIONS_PER_DEVICE (8u)
#define ZX_PCI_MAX_FUNCTIONS_PER_BUS (ZX_PCI_MAX_DEVICES_PER_BUS * ZX_PCI_MAX_FUNCTIONS_PER_DEVICE)

#define ZX_PCI_MAX_LEGACY_IRQ_PINS (4u)
#define ZX_PCI_MAX_MSI_IRQS        (32u)
#define ZX_PCI_MAX_MSIX_IRQS       (2048u)

#define ZX_PCI_STANDARD_CONFIG_HDR_SIZE (64u)
#define ZX_PCI_BASE_CONFIG_SIZE         (256u)
#define ZX_PCI_EXTENDED_CONFIG_SIZE     (4096u)
#define ZX_PCI_ECAM_BYTE_PER_BUS (ZX_PCI_EXTENDED_CONFIG_SIZE * ZX_PCI_MAX_FUNCTIONS_PER_BUS)

#define ZX_PCI_BAR_REGS_PER_BRIDGE    (2u)
#define ZX_PCI_BAR_REGS_PER_DEVICE    (6u)
#define ZX_PCI_MAX_BAR_REGS           (6u)

#define ZX_PCI_NO_IRQ_MAPPING UINT32_MAX

// Used for zx_pci_init_arg_t::addr_windows::cfg_space_type
#define PCI_CFG_SPACE_TYPE_PIO     (0u)
#define PCI_CFG_SPACE_TYPE_MMIO    (1u)
#define PCI_CFG_SPACE_TYPE_DW_ROOT (2u)  // Designware Root Bridge ECAM
#define PCI_CFG_SPACE_TYPE_DW_DS   (3u)  // Designware Downstream ECAM

// Dimensions: device id, function id, legacy pin number
// ZX_PCI_NO_IRQ_MAPPING if no mapping specified.
typedef uint32_t zx_pci_irq_swizzle_lut_t[ZX_PCI_MAX_DEVICES_PER_BUS]
                                         [ZX_PCI_MAX_FUNCTIONS_PER_DEVICE]
                                         [ZX_PCI_MAX_LEGACY_IRQ_PINS];

typedef struct zx_pci_init_arg {
    zx_pci_irq_swizzle_lut_t dev_pin_to_global_irq;

    uint32_t num_irqs;
    struct {
        uint32_t global_irq;
        bool level_triggered;
        bool active_high;
    } irqs[64];

    uint32_t addr_window_count;
    struct {
        uint64_t base;
        size_t size;
        uint8_t bus_start;
        uint8_t bus_end;
        uint8_t cfg_space_type;
        bool has_ecam;
    } addr_windows[];
} zx_pci_init_arg_t;

#define ZX_PCI_INIT_ARG_MAX_ECAM_WINDOWS 2
#define ZX_PCI_INIT_ARG_MAX_SIZE (sizeof(((zx_pci_init_arg_t*)NULL)->addr_windows[0]) * \
                                  ZX_PCI_INIT_ARG_MAX_ECAM_WINDOWS + \
                                  sizeof(zx_pci_init_arg_t))

// Enum used to select PCIe IRQ modes
typedef uint32_t zx_pci_irq_mode_t;
#define ZX_PCIE_IRQ_MODE_DISABLED ((zx_pci_irq_mode_t) 0u)
#define ZX_PCIE_IRQ_MODE_LEGACY ((zx_pci_irq_mode_t) 1u)
#define ZX_PCIE_IRQ_MODE_MSI ((zx_pci_irq_mode_t) 2u)
#define ZX_PCIE_IRQ_MODE_MSI_X ((zx_pci_irq_mode_t) 3u)

__END_CDECLS
