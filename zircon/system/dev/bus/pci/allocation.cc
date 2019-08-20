// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <err.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <zircon/rights.h>

#include <cassert>
#include <cinttypes>
#include <cstring>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>

#include "common.h"
#include "root.h"
#include "upstream_node.h"

namespace pci {

zx_status_t PciAllocation::CreateVmObject(zx::vmo* out_vmo) const {
  pci_tracef("Creating vmo for allocation [base = %#lx, size = %#zx]\n", base(), size());
  return zx::vmo::create_physical(resource(), base(), size(), out_vmo);
}

zx_status_t PciRootAllocator::AllocateWindow(zx_paddr_t in_base, size_t size,
                                             std::unique_ptr<PciAllocation>* out_alloc) {
  zx_paddr_t out_base;
  zx::resource res;
  zx_status_t status = pciroot_.GetAddressSpace(size, in_base, type_, low_, &out_base, &res);
  if (status != ZX_OK) {
    pci_errorf("failed to allocate [%#8lx, %#8lx, %s] from root: %d\n", in_base, size,
               (type_ == PCI_ADDRESS_SPACE_MMIO) ? "mmio" : "io", status);
    return status;
  }

  auto cleanup = fbl::MakeAutoCall([&]() { pciroot_.FreeAddressSpace(out_base, size, type_); });

  *out_alloc = std::make_unique<PciRootAllocation>(pciroot_, type_, std::move(res), out_base, size);
  cleanup.cancel();
  return ZX_OK;
}

zx_status_t PciRootAllocator::GrantAddressSpace(std::unique_ptr<PciAllocation> alloc) {
  // PciRootAllocations will free any space they hold when they are destroyed,
  // and nothing grants anything to PciRootAllocator.
  alloc.release();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PciRegionAllocator::AllocateWindow(zx_paddr_t base, size_t size,
                                               std::unique_ptr<PciAllocation>* out_alloc) {
  if (!backing_alloc_) {
    return ZX_ERR_NO_MEMORY;
  }

  RegionAllocator::Region::UPtr region_uptr;
  zx_status_t status;
  // Only use base if it is non-zero. RegionAllocator's interface is overloaded so we have
  // to call it differently.
  if (base) {
    ralloc_region_t request = {
        .base = base,
        .size = size,
    };
    status = allocator_.GetRegion(request, region_uptr);
  } else {
    status = allocator_.GetRegion(size, region_uptr);
  }

  if (status != ZX_OK) {
    return status;
  }

  zx::resource out_resource;
  // TODO(ZX-3146): When the resource subset CL lands, make this a smaller resource.
  status = backing_alloc_->resource().duplicate(ZX_DEFAULT_RESOURCE_RIGHTS, &out_resource);
  if (status != ZX_OK) {
    return status;
  }

  pci_tracef("bridge: assigned [ %#lx-%#lx ] to downstream\n", region_uptr->base,
             region_uptr->base + size);

  *out_alloc =
      std::make_unique<PciRegionAllocation>(std::move(out_resource), std::move(region_uptr));
  return ZX_OK;
}

zx_status_t PciRegionAllocator::GrantAddressSpace(std::unique_ptr<PciAllocation> alloc) {
  ZX_DEBUG_ASSERT(!backing_alloc_);

  backing_alloc_ = std::move(alloc);
  auto base = backing_alloc_->base();
  auto size = backing_alloc_->size();
  return allocator_.AddRegion({.base = base, .size = size});
}

}  // namespace pci
