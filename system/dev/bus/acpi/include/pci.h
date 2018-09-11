// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <acpica/acpi.h>
#include <acpica/actypes.h>
#include <zircon/syscalls/pci.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS;

// It would be nice to use the hwreg library here, but these structs should be kept
// simple so that it can be passed across process boundaries.

// Base Address Allocation Structure, defined in PCI firmware spec v3.2 chapter 4.1.2
typedef struct pci_ecam_baas {
    uint64_t base_address;
    uint16_t segment_group;
    uint8_t start_bus_num;
    uint8_t end_bus_num;
    uint32_t reserved0;
} pci_mcfg_allocation_t;

zx_status_t pci_init(void);
void register_pci_root(ACPI_HANDLE dev_obj);

zx_status_t get_pci_init_arg(zx_pci_init_arg_t** arg, uint32_t* size);
zx_status_t pci_report_current_resources(zx_handle_t root_resource_handle);

__END_CDECLS;
