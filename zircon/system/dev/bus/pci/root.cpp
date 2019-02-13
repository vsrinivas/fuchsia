// Copyright 2018 The Fuchsia Authors
// Copyright (c) 2018, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "root.h"
#include "common.h"
#include "upstream_node.h"
#include <assert.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>
#include <inttypes.h>
#include <lib/zx/resource.h>
#include <string.h>

namespace pci {

zx_status_t PciRootAllocator::GetRegion(zx_paddr_t in_base,
                                        size_t size,
                                        fbl::unique_ptr<PciAllocation>* alloc) {

    zx_paddr_t out_base;
    zx::resource res;
    zx_status_t status = pciroot_->GetAddressSpace(size, in_base, type_, low_, &out_base, &res);
    if (status != ZX_OK) {
        pci_errorf("failed to allocate [%#8lx, %#8lx, %s] from root: %d\n", in_base, size,
                   (type_ == PCI_ADDRESS_SPACE_MMIO) ? "mmio" : "io", status);
        return status;
    }

    auto cleanup = fbl::MakeAutoCall([&]() { pciroot_->FreeAddressSpace(out_base, size, type_); });

    fbl::AllocChecker ac;
    *alloc = fbl::unique_ptr(new (&ac) PciRootAllocation(std::move(res), out_base, size));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t PciRootAllocator::AddAddressSpace(fbl::unique_ptr<PciAllocation> alloc) {
    // The unique_ptr we've taken ownership of will handle cleaning up the alloc
    // itself and the resource it holds. The remaining task is to notify pciroot
    // that a given space can be added back to the allocators shared amongst all
    // the pci buses.
    return pciroot_->FreeAddressSpace(alloc->base(), alloc->size(), type_);
}

} // namespace pci
