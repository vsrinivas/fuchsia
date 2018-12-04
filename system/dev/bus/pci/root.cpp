// Copyright 2018 The Fuchsia Authors
// Copyright (c) 2018, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <fbl/limits.h>
#include <inttypes.h>
#include <string.h>
#include "common.h"
#include "root.h"
#include "upstream_node.h"

namespace pci {

zx_status_t PciRootAllocator::GetRegion(zx_paddr_t base,
                                        size_t size,
                                        fbl::unique_ptr<PciAllocation> alloc) {
    zx_status_t status = pciroot_->GetAddressSpace(size, type_, low_, &base);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    alloc = fbl::unique_ptr(new (&ac) PciRootAllocation(base, size));
    if (status != ZX_OK) {
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

zx_status_t PciRootAllocator::AddAddressSpace(fbl::unique_ptr<PciAllocation> alloc) {
    // A PciRootAllocation isn't backed by any lifecycle tracked bookkeeping
    // so we don't need to worry about explicitly cleaning it up outside of
    // resetting the container unique_ptr.
    return pciroot_->FreeAddressSpace(alloc->base(), alloc->size(), type_);
}

} // namespace pci
