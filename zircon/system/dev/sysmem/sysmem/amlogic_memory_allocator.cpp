// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic_memory_allocator.h"

#include <ddk/driver.h>

#include "macros.h"

AmlogicMemoryAllocator::AmlogicMemoryAllocator(zx::bti bti)
    : bti_(std::move(bti)),
      protected_allocator_(RegionAllocator::RegionPool::Create(std::numeric_limits<size_t>::max())) {}

zx_status_t AmlogicMemoryAllocator::Init(uint64_t size) {
    // Request 64kB alignment because the hardware can only modify protections along 64kB boundaries.
    zx_status_t status = zx::vmo::create_contiguous(bti_, size, 16, &contiguous_vmo_);

    zx_paddr_t addrs;
    zx::pmt pmt;
    status = bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS,
                      contiguous_vmo_, 0, size, &addrs, 1, &pmt);
    if (status != ZX_OK) {
        DRIVER_ERROR("Could not pin memory\n");
        return status;
    }

    start_ = addrs;
    size_ = size;
    ralloc_region_t region = {start_, size_};
    protected_allocator_.AddRegion(region);
    return ZX_OK;
}

zx_status_t AmlogicMemoryAllocator::Allocate(size_t size, zx::vmo* vmo) {
    // Try to clean up all unused outstanding regions.
    for (uint32_t i = 0; i < regions_.size();) {
        fbl::unique_ptr<Region>& region = regions_[i];
        zx_info_handle_count_t count;
        zx_status_t status = region->vmo.get_info(ZX_INFO_HANDLE_COUNT, &count, sizeof(count),
                                                  nullptr, nullptr);
        ZX_ASSERT(status == ZX_OK);
        zx_info_vmo_t vmo_info;
        status = region->vmo.get_info(ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr, nullptr);
        ZX_ASSERT(status == ZX_OK);

        // This is racy because a syscall using the handle (e.g. a map) could be in progress while
        // the handle is being closed on another thread, which would allow it to later be mapped
        // even if there's no other handle.
        // TODO: Hand out clones of a VMO using a non-COW clone (once that's implemented), then use
        // ZX_VMO_ZERO_CHILDREN to determine when no other references exist.
        if (count.handle_count == 1 && vmo_info.num_mappings == 0) {
            regions_.erase(i);
        } else {
            i++;
        }
    }

    auto region = std::make_unique<Region>();
    zx_status_t status = protected_allocator_.GetRegion(size, ZX_PAGE_SIZE, region->region);

    if (status != ZX_OK) {
        DRIVER_INFO("GetRegion failed (out of space?)\n");
        return status;
    }
    // The VMO created here is a sub-region of contiguous_vmo_.
    // TODO: stop handing out phsyical VMOs when we can hand out non-COW clone VMOs instead.
    // Please do not use get_root_resource() in new code. See ZX-1497.
    status = zx_vmo_create_physical(get_root_resource(), region->region->base, size,
                                    vmo->reset_and_get_address());
    if (status != ZX_OK) {
        DRIVER_ERROR("Failed to create physical VMO: %d\n", status);
        return status;
    }
    status = vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &region->vmo);
    if (status != ZX_OK) {
        DRIVER_ERROR("Failed to create duplicate VMO: %d\n", status);
        return status;
    }
    return ZX_OK;
}

zx_status_t AmlogicMemoryAllocator::GetProtectedMemoryInfo(uint64_t* base, uint64_t* size) {
    *base = start_;
    *size = size_;
    return ZX_OK;
}
