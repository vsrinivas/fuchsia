// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/listnode.h>   // for contiainerof
#include <region-alloc/region-alloc.h>

extern "C" {

mx_status_t ralloc_create_pool(size_t slab_size, size_t max_memory, ralloc_pool_t** out_pool) {
    if (out_pool == NULL)
        return ERR_INVALID_ARGS;

    auto pool = RegionAllocator::RegionPool::Create(slab_size, max_memory);
    if (pool == nullptr)
        return ERR_NO_MEMORY;

    // Everything looks good.  Deliberately leak our reference out into the cold
    // cruel world of C.  I sure hope that it comes back some day...
    *out_pool = reinterpret_cast<ralloc_pool_t*>(pool.leak_ref());

    return NO_ERROR;
}

void ralloc_release_pool(ralloc_pool_t* pool) {
    DEBUG_ASSERT(pool != nullptr);

    // Relclaim our reference back from the land of C by turning the pointer
    // back into a RefPtr, then deliberately let it go out of scope, dropping
    // its reference and destructing the RegionPool if need be.
    auto release_me = mxtl::internal::MakeRefPtrNoAdopt(
            reinterpret_cast<RegionAllocator::RegionPool*>(pool));
}

mx_status_t ralloc_create_allocator(ralloc_allocator_t** out_allocator) {
    if (!out_allocator)
        return ERR_INVALID_ARGS;

    void* mem = ::malloc(sizeof(RegionAllocator));
    if (!mem)
        return ERR_NO_MEMORY;

    *out_allocator = reinterpret_cast<ralloc_allocator_t*>(new (mem) RegionAllocator());
    return NO_ERROR;
}

mx_status_t ralloc_set_region_pool(ralloc_allocator_t* allocator, ralloc_pool* pool) {
    if (!allocator || !pool)
        return ERR_INVALID_ARGS;

    RegionAllocator& alloc = *(reinterpret_cast<RegionAllocator*>(allocator));

    // Turn our C-style pointer back into a RefPtr<> without adding a reference,
    // then use it to call the RegionAllocator::SetRegionPool method.  Finally,
    // deliberately leak the reference again so we are not accidentally removing
    // the unmanaged reference held by our C user.
    auto pool_ref = mxtl::internal::MakeRefPtrNoAdopt(
            reinterpret_cast<RegionAllocator::RegionPool*>(pool));
    mx_status_t ret = alloc.SetRegionPool(pool_ref);
    __UNUSED auto leak = pool_ref.leak_ref();

    return ret;
}

void ralloc_reset_allocator(ralloc_allocator_t* allocator) {
    DEBUG_ASSERT(allocator);
    reinterpret_cast<RegionAllocator*>(allocator)->Reset();
}

void ralloc_destroy_allocator(ralloc_allocator_t* allocator) {
    DEBUG_ASSERT(allocator);

    RegionAllocator* alloc = reinterpret_cast<RegionAllocator*>(allocator);
    alloc->~RegionAllocator();
    ::free(alloc);
}

mx_status_t ralloc_add_region(ralloc_allocator_t* allocator,
                                   const ralloc_region_t* region) {
    if (!allocator || !region)
        return ERR_INVALID_ARGS;

    return reinterpret_cast<RegionAllocator*>(allocator)->AddRegion(*region);
}

mx_status_t ralloc_get_sized_region_ex(ralloc_allocator_t* allocator,
                                       uint64_t size,
                                       uint64_t alignment,
                                       const ralloc_region_t** out_region) {
    if (!allocator || !out_region)
        return ERR_INVALID_ARGS;

    RegionAllocator::Region::UPtr managed_region;
    RegionAllocator& alloc  = *(reinterpret_cast<RegionAllocator*>(allocator));
    mx_status_t     result = alloc.GetRegion(size, alignment, managed_region);

    if (result == NO_ERROR) {
        // Everything looks good.  Detach the managed_region our unique_ptr<>
        // and send the unmanaged pointer to the inner ralloc_region_t back
        // to the caller.
        DEBUG_ASSERT(managed_region != nullptr);
        const RegionAllocator::Region* raw_region = managed_region.release();
        *out_region = static_cast<const ralloc_region_t*>(raw_region);
    } else {
        DEBUG_ASSERT(managed_region == nullptr);
        *out_region = NULL;
    }

    return result;
}

mx_status_t ralloc_get_specific_region_ex(
        ralloc_allocator_t*     allocator,
        const ralloc_region_t*  requested_region,
        const ralloc_region_t** out_region) {
    if (!allocator || !requested_region || !out_region)
        return ERR_INVALID_ARGS;

    RegionAllocator::Region::UPtr managed_region;
    RegionAllocator& alloc  = *(reinterpret_cast<RegionAllocator*>(allocator));
    mx_status_t     result = alloc.GetRegion(*requested_region, managed_region);

    if (result == NO_ERROR) {
        // Everything looks good.  Detach the managed_region our unique_ptr<>
        // and send the unmanaged pointer to the inner ralloc_region_t back
        // to the caller.
        DEBUG_ASSERT(managed_region != nullptr);
        const RegionAllocator::Region* raw_region = managed_region.release();
        *out_region = static_cast<const ralloc_region_t*>(raw_region);
    } else {
        DEBUG_ASSERT(managed_region == nullptr);
        *out_region = NULL;
    }

    return result;
}

void ralloc_put_region(const ralloc_region_t* region) {
    DEBUG_ASSERT(region);

    // Relclaim our reference back from the land of C by turning the pointer
    // back into a unique_ptr, then deliberately let it go out of scope, destroying the
    // RegionAllocator::Region in the process..
    auto raw_region = static_cast<const RegionAllocator::Region*>(region);
    RegionAllocator::Region::UPtr release_me(raw_region);
}

}   // extern "C"
