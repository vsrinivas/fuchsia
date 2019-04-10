// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "common.h"
#include "root.h"
#include "upstream_node.h"
#include <assert.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <inttypes.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/rights.h>

namespace pci {

zx_status_t PciAllocation::CreateVmObject(fbl::unique_ptr<zx::vmo>* out_vmo) const {
    zx::vmo temp;
    zx_status_t status = zx::vmo::create_physical(resource_, base(), size(), &temp);
    if (status != ZX_OK) {
        return status;
    }

    *out_vmo = fbl::make_unique<zx::vmo>(std::move(temp));
    return status;
}

zx_status_t PciRootAllocator::GetRegion(zx_paddr_t in_base,
                                        size_t size,
                                        fbl::unique_ptr<PciAllocation>* alloc) {

    zx_paddr_t out_base;
    zx::resource res;
    zx_status_t status = pciroot_.GetAddressSpace(size, in_base, type_, low_, &out_base, &res);
    if (status != ZX_OK) {
        pci_errorf("failed to allocate [%#8lx, %#8lx, %s] from root: %d\n", in_base, size,
                   (type_ == PCI_ADDRESS_SPACE_MMIO) ? "mmio" : "io", status);
        return status;
    }

    auto cleanup = fbl::MakeAutoCall([&]() { pciroot_.FreeAddressSpace(out_base, size, type_); });

    fbl::AllocChecker ac;
    *alloc = fbl::unique_ptr<PciRootAllocation>(new (&ac) PciRootAllocation(pciroot_, type_,
                                                                            std::move(res),
                                                                            out_base, size));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t PciRootAllocator::AddAddressSpace(fbl::unique_ptr<PciAllocation> alloc) {
    // PciRootAllocations will free any space they hold when they are destroyed,
    // and nothing seeds a PciRootAllocator.
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PciRegionAllocator::GetRegion(zx_paddr_t base,
                                          size_t size,
                                          fbl::unique_ptr<PciAllocation>* alloc) {
    RegionAllocator::Region::UPtr region_uptr;
    zx_status_t status;
    // Only use base if it is non-zero
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
    status = backing_alloc_->resource().duplicate(ZX_DEFAULT_RESOURCE_RIGHTS, &out_resource);
    if (status != ZX_OK) {
        return status;
    }

    pci_tracef("bridge: assigned [ %#lx-%#lx ] to downstream\n", region_uptr->base,
               region_uptr->base + size);
    fbl::AllocChecker ac;
    *alloc =
        fbl::unique_ptr<PciRegionAllocation>(new (&ac) PciRegionAllocation(std::move(out_resource),
                                                                           std::move(region_uptr)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

zx_status_t PciRegionAllocator::AddAddressSpace(fbl::unique_ptr<PciAllocation> alloc) {
    backing_alloc_ = std::move(alloc);
    auto base = backing_alloc_->base();
    auto size = backing_alloc_->size();
    return allocator_.AddRegion({.base = base, .size = size});
}

} // namespace pci
