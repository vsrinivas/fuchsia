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
#include <bits/limits.h>

#include "methods.h"
#include "pci.h"
#include "resources.h"

static fbl::Vector<pci_mcfg_allocation_t*> mcfg_allocations;
const char* kLogTag = "acpi-pci:";

// These allocators container available regions of physical address space in the memory
// map that we should be able to allocate bars from. Different allocators exist for 32
// and 64 bit bars so that we can be sure addresses at < 4GB are preserved for 32 bit
// bars.
RegionAllocator kMmio32Alloc;
RegionAllocator kMmio64Alloc;
RegionAllocator kIoAlloc;

struct report_current_resources_ctx {
    zx_handle_t pci_handle;
    bool device_is_root_bridge;
    bool add_pass;
};

static ACPI_STATUS report_current_resources_resource_cb_ex(ACPI_RESOURCE* res, void* _ctx) {
    auto* ctx = static_cast<struct report_current_resources_ctx*>(_ctx);
    zx_status_t status;

    bool is_mmio = false;
    uint64_t base = 0;
    uint64_t len = 0;
    bool add_range = false;

    if (resource_is_memory(res)) {
        resource_memory_t mem;
        status = resource_parse_memory(res, &mem);
        if (status != ZX_OK || mem.minimum != mem.maximum) {
            return AE_ERROR;
        }

        is_mmio = true;
        base = mem.minimum;
        len = mem.address_length;
    } else if (resource_is_address(res)) {
        resource_address_t addr;
        status = resource_parse_address(res, &addr);
        if (status != ZX_OK) {
            return AE_ERROR;
        }

        if (addr.resource_type == RESOURCE_ADDRESS_MEMORY) {
            is_mmio = true;
        } else if (addr.resource_type == RESOURCE_ADDRESS_IO) {
            is_mmio = false;
        } else {
            return AE_OK;
        }

        if (!addr.min_address_fixed || !addr.max_address_fixed || addr.maximum < addr.minimum) {
            printf("WARNING: ACPI found bad _CRS address entry\n");
            return AE_OK;
        }

        // We compute len from maximum rather than address_length, since some
        // implementations don't set address_length...
        base = addr.minimum;
        len = addr.maximum - base + 1;

        // PCI root bridges report downstream resources via _CRS.  Since we're
        // gathering data on acceptable ranges for PCI to use for MMIO, consider
        // non-consume-only address resources to be valid for PCI MMIO.
        if (ctx->device_is_root_bridge && !addr.consumed_only) {
            add_range = true;
        }
    } else if (resource_is_io(res)) {
        resource_io_t io;
        status = resource_parse_io(res, &io);
        if (status != ZX_OK) {
            return AE_ERROR;
        }

        if (io.minimum != io.maximum) {
            printf("WARNING: ACPI found bad _CRS IO entry\n");
            return AE_OK;
        }

        is_mmio = false;
        base = io.minimum;
        len = io.address_length;
    } else {
        return AE_OK;
    }

    // Ignore empty regions that are reported, and skip any resources that
    // aren't for the pass we're doing.
    if (len == 0 || add_range != ctx->add_pass) {
        return AE_OK;
    }

    if (add_range && is_mmio && base < 1024 * 1024) {
        // The PC platform defines many legacy regions below 1MB that we do not
        // want PCIe to try to map onto.
        zxlogf(INFO, "Skipping adding MMIO range, due to being below 1MB\n");
        return AE_OK;
    }

    // Add/Subtract the [base, len] region we found through ACPI to the allocators
    // that PCI can use to allocate BARs.
    RegionAllocator* alloc;
    if (is_mmio) {
        if (base + len < UINT32_MAX) {
            alloc = &kMmio32Alloc;
        } else {
            alloc = &kMmio64Alloc;
        }
    } else {
        alloc = &kIoAlloc;
    }

    zxlogf(TRACE, "ACPI range modification: %sing %s %016lx %016lx\n",
           add_range ? "add" : "subtract", is_mmio ? "MMIO" : "PIO", base, len);
    if (add_range) {
        status = alloc->AddRegion({ .base = base, .size = len }, true);
    } else {
        status = alloc->SubtractRegion({ .base = base, .size = len }, true);
    }

    if (status != ZX_OK) {
        if (add_range) {
            zxlogf(INFO, "Failed to add range: %d\n", status);
        } else {
            // If we are subtracting a range and fail, abort.  This is bad.
            return AE_ERROR;
        }
    }
    return AE_OK;
}

static ACPI_STATUS report_current_resources_device_cb_ex(
        ACPI_HANDLE object, uint32_t nesting_level, void* _ctx, void** ret) {

    ACPI_DEVICE_INFO* info = NULL;
    ACPI_STATUS status = AcpiGetObjectInfo(object, &info);
    if (status != AE_OK) {
        return status;
    }

    auto* ctx = static_cast<struct report_current_resources_ctx*>(_ctx);
    ctx->device_is_root_bridge = (info->Flags & ACPI_PCI_ROOT_BRIDGE) != 0;

    ACPI_FREE(info);

    status = AcpiWalkResources(object, (char*)"_CRS", report_current_resources_resource_cb_ex, ctx);
    if (status == AE_NOT_FOUND || status == AE_OK) {
        return AE_OK;
    }
    return status;
}

/* @brief Report current resources to the kernel PCI driver
 *
 * Walks the ACPI namespace and use the reported current resources to inform
   the kernel PCI interface about what memory it shouldn't use.
 *
 * @param root_resource_handle The handle to pass to the kernel when talking
 * to the PCI driver.
 *
 * @return ZX_OK on success
 */
zx_status_t pci_report_current_resources_ex(zx_handle_t root_resource_handle) {
    // First we search for resources to add, then we subtract out things that
    // are being consumed elsewhere.  This forces an ordering on the
    // operations so that it should be consistent, and should protect against
    // inconistencies in the _CRS methods.

    // Walk the device tree and add to the PCIe IO ranges any resources
    // "produced" by the PCI root in the ACPI namespace.
    struct report_current_resources_ctx ctx = {
        .pci_handle = root_resource_handle,
        .device_is_root_bridge = false,
        .add_pass = true,
    };
    ACPI_STATUS status = AcpiGetDevices(NULL, report_current_resources_device_cb_ex, &ctx, NULL);
    if (status != AE_OK) {
        return ZX_ERR_INTERNAL;
    }

    // Removes resources we believe are in use by other parts of the platform
    ctx = (struct report_current_resources_ctx){
        .pci_handle = root_resource_handle,
        .device_is_root_bridge = false,
        .add_pass = false,
    };
    status = AcpiGetDevices(NULL, report_current_resources_device_cb_ex, &ctx, NULL);
    if (status != AE_OK) {
        return ZX_ERR_INTERNAL;
    }


    return ZX_OK;
}

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

zx_status_t pci_init(void) {
    auto region_pool = RegionAllocator::RegionPool::Create(PAGE_SIZE);
    if (region_pool == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = kMmio32Alloc.SetRegionPool(region_pool);
    if (status != ZX_OK) {
        return status;
    }
    status = kMmio64Alloc.SetRegionPool(region_pool);
    if (status != ZX_OK) {
        return status;
    }
    status = kIoAlloc.SetRegionPool(region_pool);
    if (status != ZX_OK) {
        return status;
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
    // Initialize the PCI allocators
    // MCFG will not exist on legacy PCI systems.
    zx_status_t status = pci_read_mcfg_table();
    if (status != ZX_OK && status != ZX_ERR_NOT_FOUND) {
        zxlogf(ERROR, "%s error attempting to read mcfg table %d\n", kLogTag, status);
        return;
    }

    status = pci_init();
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to initialize PCI allocators %d\n", kLogTag, status);
        return;
    }

    status = pci_report_current_resources_ex(get_root_resource());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s error attempting to populate PCI allocators %d\n", kLogTag, status);
        return;
    }
}
