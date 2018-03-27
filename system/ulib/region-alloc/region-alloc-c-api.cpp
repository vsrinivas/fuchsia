// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/listnode.h>   // for contiainerof
#include <region-alloc/region-alloc.h>

extern "C" {

zx_status_t ralloc_create_pool(size_t max_memory, ralloc_pool_t** out_pool) {
    if (out_pool == nullptr)
        return ZX_ERR_INVALID_ARGS;

    auto pool = RegionAllocator::RegionPool::Create(max_memory);
    if (pool == nullptr)
        return ZX_ERR_NO_MEMORY;

    // Everything looks good.  Deliberately leak our reference out into the cold
    // cruel world of C.  I sure hope that it comes back some day...
    *out_pool = reinterpret_cast<ralloc_pool_t*>(pool.leak_ref());

    return ZX_OK;
}

void ralloc_release_pool(ralloc_pool_t* pool) {
    ZX_DEBUG_ASSERT(pool != nullptr);

    // Relclaim our reference back from the land of C by turning the pointer
    // back into a RefPtr, then deliberately let it go out of scope, dropping
    // its reference and destructing the RegionPool if need be.
    auto release_me = fbl::internal::MakeRefPtrNoAdopt(
            reinterpret_cast<RegionAllocator::RegionPool*>(pool));
}

zx_status_t ralloc_create_allocator(ralloc_allocator_t** out_allocator) {
    if (!out_allocator)
        return ZX_ERR_INVALID_ARGS;

    void* mem = ::malloc(sizeof(RegionAllocator));
    if (!mem)
        return ZX_ERR_NO_MEMORY;

    *out_allocator = reinterpret_cast<ralloc_allocator_t*>(new (mem) RegionAllocator());
    return ZX_OK;
}

zx_status_t ralloc_set_region_pool(ralloc_allocator_t* allocator, ralloc_pool* pool) {
    if (!allocator || !pool)
        return ZX_ERR_INVALID_ARGS;

    RegionAllocator& alloc = *(reinterpret_cast<RegionAllocator*>(allocator));

    // Turn our C-style pointer back into a RefPtr<> without adding a reference,
    // then use it to call the RegionAllocator::SetRegionPool method.  Finally,
    // deliberately leak the reference again so we are not accidentally removing
    // the unmanaged reference held by our C user.
    auto pool_ref = fbl::internal::MakeRefPtrNoAdopt(
            reinterpret_cast<RegionAllocator::RegionPool*>(pool));
    zx_status_t ret = alloc.SetRegionPool(pool_ref);
    __UNUSED auto leak = pool_ref.leak_ref();

    return ret;
}

void ralloc_reset_allocator(ralloc_allocator_t* allocator) {
    ZX_DEBUG_ASSERT(allocator);
    reinterpret_cast<RegionAllocator*>(allocator)->Reset();
}

void ralloc_destroy_allocator(ralloc_allocator_t* allocator) {
    ZX_DEBUG_ASSERT(allocator);

    RegionAllocator* alloc = reinterpret_cast<RegionAllocator*>(allocator);
    alloc->~RegionAllocator();
    ::free(alloc);
}

zx_status_t ralloc_add_region(ralloc_allocator_t* allocator,
                              const ralloc_region_t* region,
                              bool allow_overlap) {
    if (!allocator || !region)
        return ZX_ERR_INVALID_ARGS;

    return reinterpret_cast<RegionAllocator*>(allocator)->AddRegion(*region, allow_overlap);
}

zx_status_t ralloc_sub_region(ralloc_allocator_t* allocator,
                              const ralloc_region_t* region,
                              bool allow_incomplete) {
    if (!allocator || !region)
        return ZX_ERR_INVALID_ARGS;

    return reinterpret_cast<RegionAllocator*>(allocator)->SubtractRegion(*region, allow_incomplete);
}

zx_status_t ralloc_get_sized_region_ex(ralloc_allocator_t* allocator,
                                       uint64_t size,
                                       uint64_t alignment,
                                       const ralloc_region_t** out_region) {
    if (!allocator || !out_region)
        return ZX_ERR_INVALID_ARGS;

    RegionAllocator::Region::UPtr managed_region;
    RegionAllocator& alloc  = *(reinterpret_cast<RegionAllocator*>(allocator));
    zx_status_t     result = alloc.GetRegion(size, alignment, managed_region);

    if (result == ZX_OK) {
        // Everything looks good.  Detach the managed_region our unique_ptr<>
        // and send the unmanaged pointer to the inner ralloc_region_t back
        // to the caller.
        ZX_DEBUG_ASSERT(managed_region != nullptr);
        const RegionAllocator::Region* raw_region = managed_region.release();
        *out_region = static_cast<const ralloc_region_t*>(raw_region);
    } else {
        ZX_DEBUG_ASSERT(managed_region == nullptr);
        *out_region = nullptr;
    }

    return result;
}

zx_status_t ralloc_get_specific_region_ex(
        ralloc_allocator_t*     allocator,
        const ralloc_region_t*  requested_region,
        const ralloc_region_t** out_region) {
    if (!allocator || !requested_region || !out_region)
        return ZX_ERR_INVALID_ARGS;

    RegionAllocator::Region::UPtr managed_region;
    RegionAllocator& alloc  = *(reinterpret_cast<RegionAllocator*>(allocator));
    zx_status_t     result = alloc.GetRegion(*requested_region, managed_region);

    if (result == ZX_OK) {
        // Everything looks good.  Detach the managed_region our unique_ptr<>
        // and send the unmanaged pointer to the inner ralloc_region_t back
        // to the caller.
        ZX_DEBUG_ASSERT(managed_region != nullptr);
        const RegionAllocator::Region* raw_region = managed_region.release();
        *out_region = static_cast<const ralloc_region_t*>(raw_region);
    } else {
        ZX_DEBUG_ASSERT(managed_region == nullptr);
        *out_region = nullptr;
    }

    return result;
}

size_t ralloc_get_allocated_region_count(const ralloc_allocator_t* allocator) {
    ZX_DEBUG_ASSERT(allocator != nullptr);
    const RegionAllocator& alloc = *(reinterpret_cast<const RegionAllocator*>(allocator));
    return alloc.AllocatedRegionCount();
}

size_t ralloc_get_available_region_count(const ralloc_allocator_t* allocator) {
    ZX_DEBUG_ASSERT(allocator != nullptr);
    const RegionAllocator& alloc = *(reinterpret_cast<const RegionAllocator*>(allocator));
    return alloc.AvailableRegionCount();
}

void ralloc_put_region(const ralloc_region_t* region) {
    ZX_DEBUG_ASSERT(region);

    // Relclaim our reference back from the land of C by turning the pointer
    // back into a unique_ptr, then deliberately let it go out of scope, destroying the
    // RegionAllocator::Region in the process..
    auto raw_region = static_cast<const RegionAllocator::Region*>(region);
    RegionAllocator::Region::UPtr release_me(raw_region);
}

zx_status_t ralloc_walk_allocated_regions(const ralloc_allocator_t* allocator,
                                          region_walk_cb cb,
                                          void* ctx) {
    ZX_DEBUG_ASSERT(allocator != nullptr);
    if (cb == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    const RegionAllocator& alloc = *(reinterpret_cast<const RegionAllocator*>(allocator));
    alloc.WalkAllocatedRegions([cb, ctx](const ralloc_region_t* r) -> bool {
        return cb(r, ctx);
    });

    return ZX_OK;
}

}   // extern "C"
