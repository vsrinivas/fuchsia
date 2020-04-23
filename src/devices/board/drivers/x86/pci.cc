// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "pci.h"

#include <inttypes.h>
#include <lib/pci/root.h>
#include <stdio.h>
#include <string.h>

#include <memory>

#include <acpica/acpi.h>
#include <acpica/actypes.h>
#include <acpica/acuuid.h>
#include <bits/limits.h>
#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/vector.h>
#include <region-alloc/region-alloc.h>

#include "acpi-private.h"
#include "methods.h"
#include "pci.h"
#include "resources.h"

// This file contains the implementation for the code supporting the in-progress userland
// pci bus driver.

static fbl::Vector<pci_mcfg_allocation_t*> mcfg_allocations;
const char* kLogTag = "acpi-pci:";

// These allocators container available regions of physical address space in the memory
// map that we should be able to allocate bars from. Different allocators exist for 32
// and 64 bit bars so that we can be sure addresses at < 4GB are preserved for 32 bit
// bars.
RegionAllocator kMmio32Alloc;
RegionAllocator kMmio64Alloc;
RegionAllocator kIoAlloc;

const RegionAllocator* Get32BitMmioAllocator() { return &kMmio32Alloc; }
const RegionAllocator* Get64BitMmioAllocator() { return &kMmio64Alloc; }
const RegionAllocator* GetIoAllocator() { return &kIoAlloc; }

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
    zxlogf(INFO, "Skipping adding MMIO range, due to being below 1MB");
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

  zxlogf(TRACE, "ACPI range modification: %sing %s %016lx %016lx", add_range ? "add" : "subtract",
         is_mmio ? "MMIO" : "PIO", base, len);
  if (add_range) {
    status = alloc->AddRegion({.base = base, .size = len}, true);
  } else {
    status = alloc->SubtractRegion({.base = base, .size = len}, true);
  }

  if (status != ZX_OK) {
    if (add_range) {
      zxlogf(INFO, "Failed to add range: %d", status);
    } else {
      // If we are subtracting a range and fail, abort.  This is bad.
      return AE_ERROR;
    }
  }
  return AE_OK;
}

static ACPI_STATUS report_current_resources_device_cb_ex(ACPI_HANDLE object, uint32_t nesting_level,
                                                         void* _ctx, void** ret) {
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
    zxlogf(TRACE, "%s no MCFG table found.", kLogTag);
    return ZX_ERR_NOT_FOUND;
  }

  // The MCFG table contains a variable number of Extended Config tables
  // hanging off of the end.  Typically there will be one, but more
  // complicated systems may have multiple per PCI Host Bridge. The length in
  // the header is the overall size, so that is used to calculate how many
  // ECAMs are included.
  ACPI_TABLE_MCFG* mcfg = reinterpret_cast<ACPI_TABLE_MCFG*>(raw_table);
  uintptr_t table_start = reinterpret_cast<uintptr_t>(mcfg) + sizeof(ACPI_TABLE_MCFG);
  uintptr_t table_end = reinterpret_cast<uintptr_t>(mcfg) + mcfg->Header.Length;
  size_t table_bytes = table_end - table_start;
  if (table_bytes % sizeof(pci_mcfg_allocation_t)) {
    zxlogf(ERROR, "%s MCFG table has invalid size %zu", kLogTag, table_bytes);
    return ZX_ERR_INTERNAL;
  }

  // Each allocation corresponds to a particular PCI Segment Group. We'll
  // store them so that the protocol can return them for bus driver instances
  // later.
  for (unsigned i = 0; i < table_bytes / sizeof(pci_mcfg_allocation_t); i++) {
    auto entry = &(reinterpret_cast<pci_mcfg_allocation_t*>(table_start))[i];
    mcfg_allocations.push_back(entry);
    zxlogf(TRACE, "%s MCFG allocation %u (Addr = %#lx, Segment = %u, Start = %u, End = %u)",
           kLogTag, i, entry->base_address, entry->segment_group, entry->start_bus_num,
           entry->end_bus_num);
  }
  return ZX_OK;
}

bool pci_platform_has_mcfg(void) { return (mcfg_allocations.size() != 0); }

// Search the MCFG allocations found earlier for an entry matching a given
// segment a host bridge is a part of. Per the PCI Firmware spec v3 table 4-3
// note 1, a given segment group will contain only a single mcfg allocation
// entry.
zx_status_t pci_get_segment_mcfg_alloc(unsigned segment_group, pci_mcfg_allocation_t* out) {
  for (auto& entry : mcfg_allocations) {
    if (entry->segment_group == segment_group) {
      *out = *entry;
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t pci_init_bookkeeping(void) {
  zx_status_t status;
  auto region_pool = RegionAllocator::RegionPool::Create(PAGE_SIZE);
  if (region_pool == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  status = kMmio32Alloc.SetRegionPool(region_pool);
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

  // MCFG table will only exist with PCIe so 'not found' is not a failure case.
  status = pci_read_mcfg_table();
  if (status != ZX_OK && status != ZX_ERR_NOT_FOUND) {
    zxlogf(ERROR, "%s error attempting to read mcfg table %d", kLogTag, status);
    return status;
  }

  // Please do not use get_root_resource() in new code. See ZX-1467.
  status = pci_report_current_resources_ex(get_root_resource());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s error attempting to populate PCI allocators %d", kLogTag, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t pci_init(zx_device_t* parent, ACPI_HANDLE object, ACPI_DEVICE_INFO* info,
                     AcpiWalker* ctx) {
  static bool pci_bookkeeping_initialized = false;
  zx_status_t status = ZX_OK;

  // If it's the first time we've found a root we need to set up our address
  // space allocators and walk the various ACPI tables.
  if (!pci_bookkeeping_initialized) {
    status = pci_init_bookkeeping();
    if (status != ZX_OK) {
      return status;
    }
    pci_bookkeeping_initialized = true;
  }

  // Build up a context structure for the PCI Root / Host Bridge we've found.
  // If we find _BBN / _SEG we will use those, but if we don't we can fall
  // back on having an ecam from mcfg allocations.
  fbl::AllocChecker ac;
  auto dev_ctx = std::unique_ptr<pciroot_ctx_t>(new (&ac) pciroot_ctx_t());
  if (!ac.check()) {
    zxlogf(ERROR, "failed to allocate pciroot ctx: %d!", status);
    return ZX_ERR_NO_MEMORY;
  }

  dev_ctx->acpi_object = object;
  dev_ctx->acpi_device_info = *info;
  // ACPI names are stored as 4 bytes in a u32
  memcpy(dev_ctx->name, &info->Name, 4);

  status = acpi_bbn_call(object, &dev_ctx->info.start_bus_num);
  if (status != ZX_OK && status != ZX_ERR_NOT_FOUND) {
    zxlogf(TRACE, "%s Unable to read _BBN for '%s' (%d), assuming base bus of 0", kLogTag,
           dev_ctx->name, status);

    // Until we find an ecam we assume this potential legacy pci bus spans
    // bus 0 to bus 255 in its segment group.
    dev_ctx->info.end_bus_num = 255;
  }
  bool found_bbn = (status == ZX_OK);

  status = acpi_seg_call(object, &dev_ctx->info.segment_group);
  if (status != ZX_OK) {
    dev_ctx->info.segment_group = 0;
    zxlogf(TRACE, "%s Unable to read _SEG for '%s' (%d), assuming segment group 0.", kLogTag,
           dev_ctx->name, status);
  }

  // If an MCFG is found for the given segment group this root has then we'll
  // cache it for later pciroot operations and use its information to populate
  // any fields missing via _BBN / _SEG.
  auto& pinfo = dev_ctx->info;
  memcpy(pinfo.name, dev_ctx->name, sizeof(pinfo.name));
  pci_mcfg_allocation_t mcfg_alloc;
  status = pci_get_segment_mcfg_alloc(dev_ctx->info.segment_group, &mcfg_alloc);
  if (status == ZX_OK) {
    // Do the bus values make sense?
    if (found_bbn && mcfg_alloc.start_bus_num != pinfo.start_bus_num) {
      zxlogf(ERROR, "%s: conflicting base bus num for '%s', _BBN reports %u and MCFG reports %u",
             kLogTag, dev_ctx->name, pinfo.start_bus_num, mcfg_alloc.start_bus_num);
    }

    // Do the segment values make sense?
    if (pinfo.segment_group != 0 && pinfo.segment_group != mcfg_alloc.segment_group) {
      zxlogf(ERROR, "%s: conflicting segment group for '%s', _BBN reports %u and MCFG reports %u",
             kLogTag, dev_ctx->name, pinfo.segment_group, mcfg_alloc.segment_group);
    }

    // Since we have an ecam its metadata will replace anything defined in the ACPI tables.
    pinfo.segment_group = mcfg_alloc.segment_group;
    pinfo.start_bus_num = mcfg_alloc.start_bus_num;
    pinfo.end_bus_num = mcfg_alloc.end_bus_num;

    // The bus driver needs a VMO representing the entire ecam region so it can map it in.
    // The range from start_bus_num to end_bus_num is inclusive.
    size_t ecam_size = (pinfo.end_bus_num - pinfo.start_bus_num + 1) * PCIE_ECAM_BYTES_PER_BUS;
    zx_paddr_t vmo_base = mcfg_alloc.base_address + (pinfo.start_bus_num * PCIE_ECAM_BYTES_PER_BUS);
    // Please do not use get_root_resource() in new code. See ZX-1467.
    status = zx_vmo_create_physical(get_root_resource(), vmo_base, ecam_size, &pinfo.ecam_vmo);
    if (status != ZX_OK) {
      zxlogf(ERROR, "couldn't create VMO for ecam, mmio cfg will not work: %d!", status);
      return status;
    }
  }

  if (zxlog_level_enabled(TRACE)) {
    printf("%s %s { acpi_obj(%p), bus range: %u:%u, segment: %u }\n", kLogTag, dev_ctx->name,
           dev_ctx->acpi_object, pinfo.start_bus_num, pinfo.end_bus_num, pinfo.segment_group);
    if (pinfo.ecam_vmo != ZX_HANDLE_INVALID) {
      printf("%s ecam base %#" PRIxPTR "\n", kLogTag, mcfg_alloc.base_address);
    }
  }

  // These are cached here to work around dev_ctx potentially going out of scope
  // after device_add in the event that unbind/release are called from the DDK. See
  // the below TODO for more information.
  char name[5];
  uint8_t last_pci_bbn = dev_ctx->info.start_bus_num;
  memcpy(name, dev_ctx->name, sizeof(name));

  status = Pciroot::Create(std::move(dev_ctx), parent, ctx->platform_bus, name);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to add pciroot device for '%s': %d", kLogTag, dev_ctx->name, status);
  } else {
    // devmgr owns the ctx pointer now so release it from the uptr
    __UNUSED auto p = dev_ctx.release();

    // TODO(cja): these support the legacy-ish ACPI nhlt table handling that will need to be
    // updated in the future.
    ctx->found_pci = true;
    ctx->last_pci = last_pci_bbn;
    zxlogf(INFO, "%s published pciroot '%s'", kLogTag, name);
  }

  return status;
}
