// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "pci.h"

#include <inttypes.h>
#include <lib/pci/pciroot.h>
#include <lib/pci/root_host.h>
#include <lib/zx/resource.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <memory>

#include <acpica/acpi.h>
#include <acpica/actypes.h>
#include <acpica/acuuid.h>
#include <bits/limits.h>
#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/string_buffer.h>
#include <fbl/vector.h>
#include <region-alloc/region-alloc.h>

#include "acpi-private.h"
#include "methods.h"
#include "resources.h"

// This file contains the implementation for the code supporting the in-progress userland
// pci bus driver.
std::unique_ptr<PciRootHost> RootHost = {};

struct ResourceContext {
  zx_handle_t pci_handle;
  bool device_is_root_bridge;
  bool add_pass;
};

// ACPICA will call this function for each resource found while walking a device object's resource
// list.
static ACPI_STATUS resource_report_callback(ACPI_RESOURCE* res, void* _ctx) {
  auto* ctx = static_cast<ResourceContext*>(_ctx);
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

  if (add_range && is_mmio && base < MB(1)) {
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
      alloc = &RootHost->Mmio32();
    } else {
      alloc = &RootHost->Mmio64();
    }
  } else {
    alloc = &RootHost->Io();
  }

  zxlogf(DEBUG, "ACPI range modification: %sing %s %016lx %016lx", add_range ? "add" : "subtract",
         is_mmio ? "MMIO" : "PIO", base, len);
  if (add_range) {
    status = alloc->AddRegion({.base = base, .size = len}, RegionAllocator::AllowOverlap::No);
  } else {
    status =
        alloc->SubtractRegion({.base = base, .size = len}, RegionAllocator::AllowIncomplete::No);
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

// ACPICA will call this function once per device object found while walking the device
// tree off of the PCI root.
static ACPI_STATUS walk_devices_callback(ACPI_HANDLE object, uint32_t /*nesting_level*/, void* _ctx,
                                         void** /*ret*/) {
  acpi::UniquePtr<ACPI_DEVICE_INFO> info;
  if (auto res = acpi::GetObjectInfo(object); res.is_error()) {
    zxlogf(DEBUG, "bus-acpi: acpi::GetObjectInfo failed %d", res.error_value());
    return res.error_value();
  } else {
    info = std::move(res.value());
  }

  auto* ctx = static_cast<ResourceContext*>(_ctx);
  ctx->device_is_root_bridge = (info->Flags & ACPI_PCI_ROOT_BRIDGE) != 0;

  ACPI_STATUS status =
      AcpiWalkResources(object, const_cast<char*>("_CRS"), resource_report_callback, ctx);
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
zx_status_t scan_acpi_tree_for_resources(zx_handle_t root_resource_handle) {
  // First we search for resources to add, then we subtract out things that
  // are being consumed elsewhere.  This forces an ordering on the
  // operations so that it should be consistent, and should protect against
  // inconistencies in the _CRS methods.

  // Walk the device tree and add to the PCIe IO ranges any resources
  // "produced" by the PCI root in the ACPI namespace.
  ResourceContext ctx = {
      .pci_handle = root_resource_handle,
      .device_is_root_bridge = false,
      .add_pass = true,
  };
  ACPI_STATUS status = AcpiGetDevices(nullptr, walk_devices_callback, &ctx, nullptr);
  if (status != AE_OK) {
    return ZX_ERR_INTERNAL;
  }

  // Removes resources we believe are in use by other parts of the platform
  ctx.add_pass = false;
  status = AcpiGetDevices(nullptr, walk_devices_callback, &ctx, nullptr);
  if (status != AE_OK) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

// Reads the MCFG table from ACPI and caches it for later calls
// to pci_ger_segment_mcfg_alloc()
static zx_status_t read_mcfg_table(std::vector<McfgAllocation>* mcfg_table) {
  // Systems will have an MCFG table unless they only support legacy PCI.
  ACPI_TABLE_HEADER* raw_table = nullptr;
  ACPI_STATUS status = AcpiGetTable(const_cast<char*>(ACPI_SIG_MCFG), 1, &raw_table);
  if (status != AE_OK) {
    zxlogf(DEBUG, "no MCFG table found.");
    return ZX_ERR_NOT_FOUND;
  }

  // The MCFG table contains a variable number of Extended Config tables
  // hanging off of the end.  Typically there will be one, but more
  // complicated systems may have multiple per PCI Host Bridge. The length in
  // the header is the overall size, so that is used to calculate how many
  // ECAMs are included.
  auto* mcfg = reinterpret_cast<ACPI_TABLE_MCFG*>(raw_table);
  uintptr_t table_start = reinterpret_cast<uintptr_t>(mcfg) + sizeof(ACPI_TABLE_MCFG);
  uintptr_t table_end = reinterpret_cast<uintptr_t>(mcfg) + mcfg->Header.Length;
  size_t table_bytes = table_end - table_start;
  if (table_bytes % sizeof(McfgAllocation)) {
    zxlogf(ERROR, "MCFG table has invalid size %zu", table_bytes);
    return ZX_ERR_INTERNAL;
  }

  // Each allocation corresponds to a particular PCI Segment Group. We'll
  // store them so that the protocol can return them for bus driver instances
  // later.
  for (unsigned i = 0; i < table_bytes / sizeof(acpi_mcfg_allocation); i++) {
    auto entry = &(reinterpret_cast<acpi_mcfg_allocation*>(table_start))[i];
    zxlogf(DEBUG, "MCFG allocation %u (Addr = %#llx, Segment = %u, Start = %u, End = %u)", i,
           entry->Address, entry->PciSegment, entry->StartBusNumber, entry->EndBusNumber);
    mcfg_table->push_back(
        {entry->Address, entry->PciSegment, entry->StartBusNumber, entry->EndBusNumber});
  }
  return ZX_OK;
}

// Parse the MCFG table and initialize the window allocators for the RootHost if this is the first
// root found.
zx_status_t pci_root_host_init() {
  static bool initialized = false;
  if (initialized) {
    return ZX_OK;
  }

  if (!RootHost) {
    RootHost = std::make_unique<PciRootHost>(zx::unowned_resource(get_root_resource()));
  }

  zx_status_t st = read_mcfg_table(&RootHost->mcfgs());
  if (st != ZX_OK) {
    return st;
  }

  st = scan_acpi_tree_for_resources(get_root_resource());
  if (st != ZX_OK) {
    return st;
  }

  initialized = true;
  return ZX_OK;
}

zx_status_t pci_init(zx_device_t* parent, ACPI_HANDLE object, ACPI_DEVICE_INFO* info,
                     AcpiWalker* ctx) {
  pci_root_host_init();

  // Build up a context structure for the PCI Root / Host Bridge we've found.
  // If we find _BBN / _SEG we will use those, but if we don't we can fall
  // back on having an ecam from mcfg allocations.
  x64Pciroot::Context dev_ctx = {};
  dev_ctx.platform_bus = ctx->platform_bus();
  dev_ctx.acpi_object = object;
  dev_ctx.acpi_device_info = *info;
  // ACPI names are stored as 4 bytes in a u32
  memcpy(dev_ctx.name, &info->Name, 4);

  zx_status_t status = acpi_bbn_call(object, &dev_ctx.info.start_bus_num);
  if (status != ZX_OK && status != ZX_ERR_NOT_FOUND) {
    zxlogf(DEBUG, "Unable to read _BBN for '%s' (%d), assuming base bus of 0", dev_ctx.name,
           status);

    // Until we find an ecam we assume this potential legacy pci bus spans
    // bus 0 to bus 255 in its segment group.
    dev_ctx.info.end_bus_num = PCI_BUS_MAX;
  }
  bool found_bbn = (status == ZX_OK);

  status = acpi_seg_call(object, &dev_ctx.info.segment_group);
  if (status != ZX_OK) {
    dev_ctx.info.segment_group = 0;
    zxlogf(DEBUG, "Unable to read _SEG for '%s' (%d), assuming segment group 0.", dev_ctx.name,
           status);
  }

  // If an MCFG is found for the given segment group this root has then we'll
  // cache it for later pciroot operations and use its information to populate
  // any fields missing via _BBN / _SEG.
  auto& pinfo = dev_ctx.info;
  memcpy(pinfo.name, dev_ctx.name, sizeof(pinfo.name));
  McfgAllocation mcfg_alloc;
  status = RootHost->GetSegmentMcfgAllocation(dev_ctx.info.segment_group, &mcfg_alloc);
  if (status == ZX_OK) {
    // Do the bus values make sense?
    if (found_bbn && mcfg_alloc.start_bus_number != pinfo.start_bus_num) {
      zxlogf(ERROR, "conflicting base bus num for '%s', _BBN reports %u and MCFG reports %u",
             dev_ctx.name, pinfo.start_bus_num, mcfg_alloc.start_bus_number);
    }

    // Do the segment values make sense?
    if (pinfo.segment_group != 0 && pinfo.segment_group != mcfg_alloc.pci_segment) {
      zxlogf(ERROR, "conflicting segment group for '%s', _BBN reports %u and MCFG reports %u",
             dev_ctx.name, pinfo.segment_group, mcfg_alloc.pci_segment);
    }

    // Since we have an ecam its metadata will replace anything defined in the ACPI tables.
    pinfo.segment_group = mcfg_alloc.pci_segment;
    pinfo.start_bus_num = mcfg_alloc.start_bus_number;
    pinfo.end_bus_num = mcfg_alloc.end_bus_number;

    // The bus driver needs a VMO representing the entire ecam region so it can map it in.
    // The range from start_bus_num to end_bus_num is inclusive.
    size_t ecam_size = (pinfo.end_bus_num - pinfo.start_bus_num + 1) * PCIE_ECAM_BYTES_PER_BUS;
    zx_paddr_t vmo_base = mcfg_alloc.address + (pinfo.start_bus_num * PCIE_ECAM_BYTES_PER_BUS);
    // Please do not use get_root_resource() in new code. See ZX-1467.
    status = zx_vmo_create_physical(get_root_resource(), vmo_base, ecam_size, &pinfo.ecam_vmo);
    if (status != ZX_OK) {
      zxlogf(ERROR, "couldn't create VMO for ecam, mmio cfg will not work: %d!", status);
      return status;
    }
  }

  if (zxlog_level_enabled(DEBUG)) {
    fbl::StringBuffer<128> log;
    log.AppendPrintf("%s { acpi_obj(%p), bus range: %u:%u, segment: %u", dev_ctx.name,
                     dev_ctx.acpi_object, pinfo.start_bus_num, pinfo.end_bus_num,
                     pinfo.segment_group);
    if (pinfo.ecam_vmo != ZX_HANDLE_INVALID) {
      log.AppendPrintf(", ecam base: %#" PRIxPTR, mcfg_alloc.address);
    }
    log.AppendPrintf(" }");
    zxlogf(DEBUG, "%s", log.c_str());
  }

  // These are cached here to work around dev_ctx potentially going out of scope
  // after device_add in the event that unbind/release are called from the DDK. See
  // the below TODO for more information.
  char name[ZX_DEVICE_NAME_MAX] = {0};
  uint8_t last_pci_bbn = dev_ctx.info.start_bus_num;
  memcpy(name, dev_ctx.name, ACPI_NAMESEG_SIZE);

  status = x64Pciroot::Create(&*RootHost, std::move(dev_ctx), parent, name);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to add pciroot device for '%s': %d", name, status);
  } else {
    // TODO(cja): these support the legacy-ish ACPI nhlt table handling that will need to be
    // updated in the future.
    ctx->set_found_pci(true);
    *ctx->mutable_last_pci() = last_pci_bbn;
    zxlogf(INFO, "published pciroot '%s'", name);
  }

  return status;
}
