// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "pci.h"

#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/pci/pciroot.h>
#include <lib/pci/pio.h>
#include <lib/pci/root_host.h>
#include <lib/zx/resource.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/status.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <memory>

#include <acpica/acpi.h>
#include <acpica/actypes.h>
#include <acpica/acuuid.h>
#include <bits/limits.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/string_buffer.h>
#include <fbl/vector.h>
#include <region-alloc/region-alloc.h>

#include "src/devices/board/lib/acpi/acpi.h"
#include "src/devices/board/lib/acpi/manager.h"
#include "src/devices/board/lib/acpi/pci-internal.h"
#include "src/devices/board/lib/acpi/resources.h"
#include "third_party/acpica/source/include/actypes.h"

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
static acpi::status<> resource_report_callback(ACPI_RESOURCE* res, ResourceContext* ctx) {
  zx_status_t status;

  bool is_mmio = false;
  uint64_t base = 0;
  uint64_t len = 0;
  bool add_range = false;

  if (resource_is_memory(res)) {
    resource_memory_t mem;
    status = resource_parse_memory(res, &mem);
    if (status != ZX_OK || mem.minimum != mem.maximum) {
      return acpi::error(AE_ERROR);
    }

    is_mmio = true;
    base = mem.minimum;
    len = mem.address_length;
  } else if (resource_is_address(res)) {
    resource_address_t addr;
    status = resource_parse_address(res, &addr);
    if (status != ZX_OK) {
      return acpi::error(AE_ERROR);
    }

    if (addr.resource_type == RESOURCE_ADDRESS_MEMORY) {
      is_mmio = true;
    } else if (addr.resource_type == RESOURCE_ADDRESS_IO) {
      is_mmio = false;
    } else {
      return acpi::ok();
    }

    if (!addr.min_address_fixed || !addr.max_address_fixed || addr.maximum < addr.minimum) {
      zxlogf(WARNING, "ACPI found bad _CRS address entry\n");
      return acpi::ok();
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
      return acpi::error(AE_ERROR);
    }

    if (io.minimum != io.maximum) {
      zxlogf(WARNING, "ACPI found bad _CRS IO entry\n");
      return acpi::ok();
    }

    is_mmio = false;
    base = io.minimum;
    len = io.address_length;
  } else {
    return acpi::ok();
  }

  // Ignore empty regions that are reported, and skip any resources that
  // aren't for the pass we're doing.
  if (len == 0 || add_range != ctx->add_pass) {
    return acpi::ok();
  }

  if (add_range && is_mmio && base < MB(1)) {
    // The PC platform defines many legacy regions below 1MB that we do not
    // want PCIe to try to map onto.
    zxlogf(INFO, "Skipping adding MMIO range due to being below 1MB");
    return acpi::ok();
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
  // Not all resources ACPI informs us are in use are provided to us as
  // resources in the first search, so we allow Incomplete ranges in both add
  // and subtract passes.
  if (add_range) {
    status = alloc->AddRegion({.base = base, .size = len}, RegionAllocator::AllowOverlap::Yes);
  } else {
    status =
        alloc->SubtractRegion({.base = base, .size = len}, RegionAllocator::AllowIncomplete::Yes);
  }

  if (status != ZX_OK) {
    if (add_range) {
      zxlogf(INFO, "Failed to add range: [%#lx - %#lx] (%#lx): %d", base, base + len, len, status);
    } else {
      // If we are subtracting a range and fail, abort.  This is bad.
      zxlogf(INFO, "Failed to subtract range [%#lx - %#lx] (%#lx): %d", base, base + len, len,
             status);
      return acpi::error(AE_ERROR);
    }
  }
  return acpi::ok();
}

// ACPICA will call this function once per device object found while walking the device
// tree off of the PCI root.
static acpi::status<> walk_devices_callback(ACPI_HANDLE object, ResourceContext* ctx,
                                            acpi::Acpi* acpi) {
  acpi::UniquePtr<ACPI_DEVICE_INFO> info;
  auto res = acpi->GetObjectInfo(object);
  if (res.is_error()) {
    zxlogf(DEBUG, "acpi::GetObjectInfo failed %d", res.error_value());
    return res.take_error();
  }
  info = std::move(res.value());

  ctx->device_is_root_bridge = (info->Flags & ACPI_PCI_ROOT_BRIDGE) != 0;

  acpi::status<> status = acpi->WalkResources(
      object, const_cast<char*>("_CRS"),
      [ctx](ACPI_RESOURCE* rsrc) { return resource_report_callback(rsrc, ctx); });
  if (status.is_ok() || status.error_value() == AE_NOT_FOUND) {
    return acpi::ok();
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
zx_status_t scan_acpi_tree_for_resources(acpi::Acpi* acpi, zx_handle_t root_resource_handle) {
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
  acpi::status<> status =
      acpi->GetDevices(nullptr, [acpi, ctx = &ctx](ACPI_HANDLE device, uint32_t) {
        return walk_devices_callback(device, ctx, acpi);
      });
  if (status.is_error()) {
    return ZX_ERR_INTERNAL;
  }

  // Removes resources we believe are in use by other parts of the platform
  ctx.add_pass = false;
  status = acpi->GetDevices(nullptr, [acpi, ctx = &ctx](ACPI_HANDLE device, uint32_t) {
    return walk_devices_callback(device, ctx, acpi);
  });
  if (status.is_error()) {
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

zx_status_t pci_init_interrupts(acpi::Acpi* acpi, ACPI_HANDLE object,
                                x64Pciroot::Context* dev_ctx) {
  zx::vmo routing_vmo{};
  if (acpi::GetPciRootIrqRouting(acpi, object, dev_ctx) != AE_OK) {
    zxlogf(ERROR, "Failed to obtain PCI IRQ routing information, legacy IRQs will not function");
  }

  fbl::Array<pci_legacy_irq> irq_list(new pci_legacy_irq[dev_ctx->irqs.size()]{},
                                      dev_ctx->irqs.size());
  size_t irq_cnt = 0;
  fbl::StringBuffer<ZX_MAX_NAME_LEN> name = {};
  name.Append(dev_ctx->name, sizeof(dev_ctx->name));
  name.Append(" legacy");

  for (const auto& e : dev_ctx->irqs) {
    const uint32_t& vector = e.first;
    const acpi_legacy_irq& irq_cfg = e.second;
    zx::resource resource;
    zx_status_t status = zx::resource::create(*zx::unowned_resource(get_root_resource()),
                                              ZX_RSRC_KIND_IRQ | ZX_RSRC_FLAG_EXCLUSIVE, vector, 1,
                                              name.data(), name.size(), &resource);

    if (status != ZX_OK) {
      zxlogf(ERROR, "Couldn't create resource for legacy vector %#x: %s, skipping it", vector,
             zx_status_get_string(status));
      continue;
    }

    status =
        zx_interrupt_create(resource.get(), vector, irq_cfg.options, &irq_list[irq_cnt].interrupt);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Couldn't create irq for legacy vector %#x: %s, skipping it", vector,
             zx_status_get_string(status));
      continue;
    }

    dev_ctx->irq_resources.push_back(std::move(resource));
    irq_list[irq_cnt].vector = vector;
    irq_cnt++;
  }

  dev_ctx->info.legacy_irqs_list = irq_list.release();
  dev_ctx->info.legacy_irqs_count = irq_cnt;
  return ZX_OK;
}

zx_status_t pci_init_segment_and_ecam(acpi::Acpi* acpi, ACPI_HANDLE object,
                                      x64Pciroot::Context* dev_ctx) {
  auto bbn = acpi->CallBbn(object);
  if (bbn.is_error() && acpi_to_zx_status(bbn.error_value()) != ZX_ERR_NOT_FOUND) {
    zxlogf(DEBUG, "Unable to read _BBN for '%s' (%d), assuming base bus of 0", dev_ctx->name,
           bbn.error_value());

    // Until we find an ecam we assume this potential legacy pci bus spans
    // bus 0 to bus 255 in its segment group.
    dev_ctx->info.end_bus_num = PCI_BUS_MAX;
  }
  bool found_bbn = false;
  if (bbn.is_ok()) {
    dev_ctx->info.start_bus_num = bbn.value();
    found_bbn = true;
  }

  auto seg = acpi->CallSeg(object);
  if (seg.is_error()) {
    dev_ctx->info.segment_group = 0;
    zxlogf(DEBUG, "Unable to read _SEG for '%s' (%d), assuming segment group 0.", dev_ctx->name,
           seg.error_value());
  } else {
    dev_ctx->info.segment_group = seg.value();
  }

  // If an MCFG is found for the given segment group this root has then we'll
  // cache it for later pciroot operations and use its information to populate
  // any fields missing via _BBN / _SEG.
  auto& pinfo = dev_ctx->info;
  memcpy(pinfo.name, dev_ctx->name, sizeof(pinfo.name));
  McfgAllocation mcfg_alloc;
  zx_status_t status = RootHost->GetSegmentMcfgAllocation(dev_ctx->info.segment_group, &mcfg_alloc);
  if (status == ZX_OK) {
    // Print a warning if _BBN and MCFG bus numbers don't match. We'll use the
    // MCFG first if we have one, but a mismatch likely represents an error in
    // an ACPI table.
    if (found_bbn && mcfg_alloc.start_bus_number != pinfo.start_bus_num) {
      zxlogf(WARNING, "conflicting base bus num for '%s', _BBN reports %u and MCFG reports %u",
             dev_ctx->name, pinfo.start_bus_num, mcfg_alloc.start_bus_number);
    }

    // Same situation with Segment Group as with bus number above.
    if (pinfo.segment_group != 0 && pinfo.segment_group != mcfg_alloc.pci_segment) {
      zxlogf(WARNING, "conflicting segment group for '%s', _BBN reports %u and MCFG reports %u",
             dev_ctx->name, pinfo.segment_group, mcfg_alloc.pci_segment);
    }

    // Since we have an ecam its metadata will replace anything defined in the ACPI tables.
    pinfo.segment_group = mcfg_alloc.pci_segment;
    pinfo.start_bus_num = mcfg_alloc.start_bus_number;
    pinfo.end_bus_num = mcfg_alloc.end_bus_number;

    // The bus driver needs a VMO representing the entire ecam region so it can map it in.
    // The range from start_bus_num to end_bus_num is inclusive.
    size_t ecam_size = (pinfo.end_bus_num - pinfo.start_bus_num + 1) * PCIE_ECAM_BYTES_PER_BUS;
    zx_paddr_t vmo_base = mcfg_alloc.address + (pinfo.start_bus_num * PCIE_ECAM_BYTES_PER_BUS);
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    status = zx_vmo_create_physical(get_root_resource(), vmo_base, ecam_size, &pinfo.ecam_vmo);
    if (status != ZX_OK) {
      zxlogf(ERROR, "couldn't create VMO for ecam, mmio cfg will not work: %s!",
             zx_status_get_string(status));
      return status;
    }
  }

  if (zxlog_level_enabled(DEBUG)) {
    fbl::StringBuffer<128> log;
    log.AppendPrintf("%s { acpi_obj(%p), bus range: %u:%u, segment: %u", dev_ctx->name,
                     dev_ctx->acpi_object, pinfo.start_bus_num, pinfo.end_bus_num,
                     pinfo.segment_group);
    if (pinfo.ecam_vmo != ZX_HANDLE_INVALID) {
      log.AppendPrintf(", ecam base: %#" PRIxPTR, mcfg_alloc.address);
    }
    log.AppendPrintf(" }");
    zxlogf(DEBUG, "%s", log.c_str());
  }

  return ZX_OK;
}

// Parse the MCFG table and initialize the window allocators for the RootHost if this is the first
// root found.
zx_status_t pci_root_host_init(acpi::Acpi* acpi) {
  static bool initialized = false;
  if (initialized) {
    return ZX_OK;
  }

  if (!RootHost) {
    auto io_type = PCI_ADDRESS_SPACE_IO;
#ifdef __aarch64__
    io_type = PCI_ADDRESS_SPACE_MEMORY;
#endif
    RootHost = std::make_unique<PciRootHost>(zx::unowned_resource(get_root_resource()), io_type);
  }

  zx_status_t st = read_mcfg_table(&RootHost->mcfgs());
  if (st != ZX_OK) {
    zxlogf(WARNING, "Couldn't read MCFG table, PCI config MMIO will be unavailable: %s",
           zx_status_get_string(st));
  }

  st = scan_acpi_tree_for_resources(acpi, get_root_resource());
  if (st != ZX_OK) {
    zxlogf(ERROR, "Scanning acpi resources failed: %s", zx_status_get_string(st));
    return st;
  }

  initialized = true;
  return ZX_OK;
}

zx_status_t pci_init(zx_device_t* parent, ACPI_HANDLE object,
                     acpi::UniquePtr<ACPI_DEVICE_INFO> info, acpi::Manager* manager,
                     std::vector<pci_bdf_t> acpi_bdfs) {
  zx_status_t status = pci_root_host_init(manager->acpi());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Error initializing PCI root host: %s", zx_status_get_string(status));
    return status;
  }

  // Build up a context structure for the PCI Root / Host Bridge we've found.
  // If we find _BBN / _SEG we will use those, but if we don't we can fall
  // back on having an ecam from mcfg allocations.
  x64Pciroot::Context dev_ctx = {};
  dev_ctx.platform_bus = parent;
  dev_ctx.acpi_object = object;
  dev_ctx.acpi_device_info = std::move(info);
  // ACPI names are stored as 4 bytes in a u32
  memcpy(dev_ctx.name, &dev_ctx.acpi_device_info->Name, ACPI_NAMESEG_SIZE);
  dev_ctx.name[sizeof(dev_ctx.name) - 1] = '\0';

  status = pci_init_segment_and_ecam(manager->acpi(), object, &dev_ctx);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Initializing %s ecam and bus information failed: %s", dev_ctx.name,
           zx_status_get_string(status));
    return status;
  }

  status = pci_init_interrupts(manager->acpi(), object, &dev_ctx);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Initializing %s interrupt information failed: %s", dev_ctx.name,
           zx_status_get_string(status));
    return status;
  }

  // These are cached here to work around dev_ctx potentially going out of scope
  // after device_add in the event that unbind/release are called from the DDK. See
  // the below TODO for more information.
  char name[ZX_DEVICE_NAME_MAX] = {0};
  memcpy(name, dev_ctx.name, sizeof(dev_ctx.name));

  status = x64Pciroot::Create(&*RootHost, std::move(dev_ctx), parent, name, std::move(acpi_bdfs));
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to add pciroot device for '%s': %d", name, status);
  } else {
    zxlogf(INFO, "published pciroot '%s'", name);
  }

  return status;
}
