// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <acpica/acpi.h>
#include <acpica/actypes.h>
#include <acpica/acuuid.h>
#include <ddk/debug.h>
#include <fbl/new.h>
#include <fbl/vector.h>
#include <stdio.h>
#include <region-alloc/region-alloc.h>

#include "methods.h"
#include "pci.h"

static fbl::Vector<pci_mcfg_allocation_t*> mcfg_allocations;
const char* kLogTag = "acpi-pci:";

// Reads the MCFG table from ACPI and caches it for later calls
// to pci_ger_segment_mcfg_alloc()
static zx_status_t pci_read_mcfg_table(void) {
    // Systems will have an MCFG table unless they only support legacy PCI.
    ACPI_TABLE_HEADER* raw_table = NULL;
    ACPI_STATUS status = AcpiGetTable(const_cast<char*>(ACPI_SIG_MCFG), 1, &raw_table);
    if (status != AE_OK) {
        zxlogf(TRACE, "%s no MCFG table found.\n", kLogTag);
        return ZX_ERR_NOT_FOUND;
    }

    // The MCFG table contains a variable number of Extended Config tables hanging off of the end.
    // Typically there will be one, but more complicated systems may have multiple per PCI
    // Host Bridge. The length in the header is the overall size, so that is used to calculate
    // how many ECAMs are included.
    ACPI_TABLE_MCFG* mcfg = reinterpret_cast<ACPI_TABLE_MCFG*>(raw_table);
    uintptr_t table_start = reinterpret_cast<uintptr_t>(mcfg) + sizeof(ACPI_TABLE_MCFG);
    uintptr_t table_end = reinterpret_cast<uintptr_t>(mcfg) + mcfg->Header.Length;
    size_t table_bytes = table_end - table_start;
    if (table_bytes % sizeof(pci_mcfg_allocation_t)) {
        zxlogf(ERROR, "%s MCFG table has invalid size %zu\n", kLogTag, table_bytes);
        return ZX_ERR_INTERNAL;
    }

    // Each allocation corresponds to a particular PCI Segment Group. We'll store them so
    // that the protocol can return them for bus driver instances later.
    for (unsigned i = 0; i < table_bytes / sizeof(pci_mcfg_allocation_t); i++) {
        auto entry = &(reinterpret_cast<pci_mcfg_allocation_t*>(table_start))[i];
        mcfg_allocations.push_back(entry);
        zxlogf(TRACE, "%s MCFG allocation %u (Address = %#lx, Segment = %u, Start = %u, End = %u)\n",
               kLogTag, i, entry->base_address, entry->segment_group, entry->start_bus_num,
               entry->end_bus_num);
    }
    return ZX_OK;
}

// Search the MCFG allocations found earlier for an entry matching a given segment a host bridge
// is a part of. Per the PCI Firmware spec v3 table 4-3 note 1, a given segment group will contain
// only a single mcfg allocation entry.
zx_status_t pci_get_segment_mcfg_alloc(unsigned segment_group, pci_mcfg_allocation_t* out) {
    for (auto& entry : mcfg_allocations) {
        if (entry->segment_group == segment_group) {
            *out = *entry;
            return ZX_OK;
        }
    }
    return ZX_ERR_NOT_FOUND;
}

void register_pci_root(ACPI_HANDLE dev_obj) {
    pci_read_mcfg_table();
}

