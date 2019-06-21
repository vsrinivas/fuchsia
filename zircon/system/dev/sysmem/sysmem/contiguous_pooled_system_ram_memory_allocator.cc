// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "contiguous_pooled_system_ram_memory_allocator.h"

#include "macros.h"

ContiguousPooledSystemRamMemoryAllocator::ContiguousPooledSystemRamMemoryAllocator(Owner* parent_device, uint64_t size)
    : parent_device_(parent_device),
      region_allocator_(RegionAllocator::RegionPool::Create(std::numeric_limits<size_t>::max())),
      size_(size) {}

zx_status_t ContiguousPooledSystemRamMemoryAllocator::Init(uint32_t alignment_log2) {
    zx_status_t status = zx::vmo::create_contiguous(parent_device_->bti(), size_, alignment_log2, &contiguous_vmo_);
    if (status != ZX_OK) {
        DRIVER_ERROR("Could allocate contiguous memory, status %d\n", status);
        return status;
    }

    zx_paddr_t addrs;
    zx::pmt pmt;
    status = parent_device_->bti().pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS,
                                       contiguous_vmo_, 0, size_, &addrs, 1, &pmt);
    if (status != ZX_OK) {
        DRIVER_ERROR("Could not pin memory, status %d\n", status);
        return status;
    }

    start_ = addrs;
    ralloc_region_t region = {start_, size_};
    region_allocator_.AddRegion(region);
    return ZX_OK;
}

zx_status_t ContiguousPooledSystemRamMemoryAllocator::Allocate(uint64_t size, zx::vmo* vmo) {
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
    // TODO: Use a fragmentation-reducing allocator (such as best fit).
    zx_status_t status = region_allocator_.GetRegion(size, ZX_PAGE_SIZE, region->region);

    if (status != ZX_OK) {
        DRIVER_INFO("GetRegion failed (out of space?)\n");
        DumpPoolStats();
        return status;
    }
    // The VMO created here is a sub-region of contiguous_vmo_.
    // TODO: stop handing out physical VMOs when we can hand out non-COW clone VMOs instead.
    // Please do not use get_root_resource() in new code. See ZX-1467.
    status = parent_device_->CreatePhysicalVmo(region->region->base, size,
                                               vmo);
    if (status != ZX_OK) {
        DRIVER_ERROR("Failed to create physical VMO: %d\n", status);
        return status;
    }
    status = vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &region->vmo);
    if (status != ZX_OK) {
        DRIVER_ERROR("Failed to create duplicate VMO: %d\n", status);
        return status;
    }
    regions_.push_back(std::move(region));
    return ZX_OK;
}

void ContiguousPooledSystemRamMemoryAllocator::DumpPoolStats() {
    uint64_t unused_size = 0;
    uint64_t max_free_size = 0;
    region_allocator_.WalkAvailableRegions([&unused_size, &max_free_size](const ralloc_region_t* r) -> bool {
        unused_size += r->size;
        max_free_size = std::max(max_free_size, r->size);
        return true;
    });

    DRIVER_ERROR("Contiguous pool unused total: %ld bytes, max free size %ld bytes\n", unused_size, max_free_size);
}
