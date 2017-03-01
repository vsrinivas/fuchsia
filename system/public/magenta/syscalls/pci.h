// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off


// Base Address Registers are accessed in userspace via the get_bar protocol method. The
// Bar is represented via a pci_resource_t struct which contains a handle pointer to a VMO
// in the case of an MMIO bar, as well as a PIO addr/size pair for the memory region
// to access if a PIO bar. In the latter case, the protocol will acquire the appropriate
// permissions for the process to write to that PIO region on that architecture.
typedef enum {
    PCI_RESOURCE_TYPE_UNUSED = 0,
    PCI_RESOURCE_TYPE_MMIO,
    PCI_RESOURCE_TYPE_PIO,
} mx_pci_resource_types_t;

// TODO(cja): This makes some assumptions that anything in an arch's PIO region
// is going to be defined as a base address and size. This will need to be
// updated to a per-platform structure in the event that doesn't pan out
// in the future.
typedef struct mx_pci_bar {
    uint32_t type;
    size_t size;
    union {
        mx_handle_t mmio_handle;
        uintptr_t pio_addr;
    };
} mx_pci_resource_t;

// Defines and structures related to mx_pci_*()
// Info returned to dev manager for PCIe devices when probing.
typedef struct mx_pcie_device_info {
    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t  base_class;
    uint8_t  sub_class;
    uint8_t  program_interface;
    uint8_t  revision_id;

    uint8_t  bus_id;
    uint8_t  dev_id;
    uint8_t  func_id;
} mx_pcie_device_info_t;

#define MX_PCI_NO_IRQ_MAPPING UINT32_MAX

// Dimensions: device id, function id, legacy pin number
// MX_PCI_NO_IRQ_MAPPING if no mapping specified.
typedef uint32_t mx_pci_irq_swizzle_lut_t[32][8][4];

typedef struct mx_pci_init_arg {
    mx_pci_irq_swizzle_lut_t dev_pin_to_global_irq;

    uint32_t num_irqs;
    struct {
        uint32_t global_irq;
        bool level_triggered;
        bool active_high;
    } irqs[64];

    uint32_t ecam_window_count;
    struct {
        uint64_t base;
        size_t size;
        uint8_t bus_start;
        uint8_t bus_end;
    } ecam_windows[];
} mx_pci_init_arg_t;

#define MX_PCI_INIT_ARG_MAX_ECAM_WINDOWS 1
#define MX_PCI_INIT_ARG_MAX_SIZE (sizeof(((mx_pci_init_arg_t*)NULL)->ecam_windows[0]) * \
                                  MX_PCI_INIT_ARG_MAX_ECAM_WINDOWS + \
                                  sizeof(mx_pci_init_arg_t))

// Enum used to select PCIe IRQ modes
typedef enum {
    MX_PCIE_IRQ_MODE_DISABLED = 0,
    MX_PCIE_IRQ_MODE_LEGACY   = 1,
    MX_PCIE_IRQ_MODE_MSI      = 2,
    MX_PCIE_IRQ_MODE_MSI_X    = 3,
} mx_pci_irq_mode_t;

__END_CDECLS
