// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/vm_object_physical.h"

#include "vm_priv.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/vm.h>
#include <lib/console.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <safeint/safe_math.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

using fbl::AutoLock;

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

VmObjectPhysical::VmObjectPhysical(paddr_t base, uint64_t size)
    : size_(size), base_(base) {
    LTRACEF("%p\n", this);
}

VmObjectPhysical::~VmObjectPhysical() {
    canary_.Assert();
    LTRACEF("%p\n", this);
}

status_t VmObjectPhysical::Create(paddr_t base, uint64_t size, fbl::RefPtr<VmObject>* obj) {
    if (!IS_PAGE_ALIGNED(base) || !IS_PAGE_ALIGNED(size) || size == 0)
        return MX_ERR_INVALID_ARGS;

    // check that base + size is a valid range
    safeint::CheckedNumeric<paddr_t> safe_base = base;
    safe_base += size - 1;
    if (!safe_base.IsValid())
        return MX_ERR_INVALID_ARGS;

    fbl::AllocChecker ac;
    auto vmo = fbl::AdoptRef<VmObject>(new (&ac) VmObjectPhysical(base, size));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    // Physical VMOs should default to uncached access.
    vmo->SetMappingCachePolicy(ARCH_MMU_FLAG_UNCACHED);

    *obj = fbl::move(vmo);

    return MX_OK;
}

void VmObjectPhysical::Dump(uint depth, bool verbose) {
    canary_.Assert();

    AutoLock a(&lock_);
    for (uint i = 0; i < depth; ++i) {
        printf("  ");
    }
    printf("object %p base %#" PRIxPTR " size %#" PRIx64 " ref %d\n", this, base_, size_, ref_count_debug());
}

// get the physical address of a page at offset
status_t VmObjectPhysical::GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                                         vm_page_t** _page, paddr_t* _pa) {
    canary_.Assert();

    if (_page)
        *_page = nullptr;

    if (offset >= size_)
        return MX_ERR_OUT_OF_RANGE;

    uint64_t pa = base_ + ROUNDDOWN(offset, PAGE_SIZE);
    if (pa > UINTPTR_MAX)
        return MX_ERR_OUT_OF_RANGE;

    *_pa = (paddr_t)pa;

    return MX_OK;
}

status_t VmObjectPhysical::LookupUser(uint64_t offset, uint64_t len, user_ptr<paddr_t> buffer,
                                      size_t buffer_size) {
    canary_.Assert();

    if (unlikely(len == 0))
        return MX_ERR_INVALID_ARGS;

    AutoLock a(&lock_);

    // verify that the range is within the object
    if (unlikely(!InRange(offset, len, size_)))
        return MX_ERR_OUT_OF_RANGE;

    uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    uint64_t end = offset + len;
    uint64_t end_page_offset = ROUNDUP(end, PAGE_SIZE);

    // compute the size of the table we'll need and make sure it fits in the user buffer
    uint64_t table_size = ((end_page_offset - start_page_offset) / PAGE_SIZE) * sizeof(paddr_t);
    if (unlikely(table_size > buffer_size))
        return MX_ERR_BUFFER_TOO_SMALL;

    size_t index = 0;
    for (uint64_t off = start_page_offset; off != end_page_offset; off += PAGE_SIZE, index++) {
        // find the physical address
        uint64_t tmp = base_ + off;
        if (tmp > UINTPTR_MAX)
            return MX_ERR_OUT_OF_RANGE;

        paddr_t pa = (paddr_t)tmp;

        // check that we didn't wrap
        DEBUG_ASSERT(pa >= base_);

        // copy it out into user space
        auto status = buffer.element_offset(index).copy_to_user(pa);
        if (unlikely(status < 0))
            return status;
    }

    return MX_OK;
}

status_t VmObjectPhysical::GetMappingCachePolicy(uint32_t* cache_policy) {
    AutoLock l(&lock_);

    if (!cache_policy) {
        return MX_ERR_INVALID_ARGS;
    }

    *cache_policy = mapping_cache_flags_;
    return MX_OK;
}

status_t VmObjectPhysical::SetMappingCachePolicy(const uint32_t cache_policy) {
    AutoLock l(&lock_);

    // Is it a valid cache flag?
    if (cache_policy & ~ARCH_MMU_FLAG_CACHE_MASK) {
        return MX_ERR_INVALID_ARGS;
    }

    // If the cache policy is already configured on this VMO and matches
    // the requested policy then this is a no-op. This is a common practice
    // in the serialio and magma drivers, but may change.
    // TODO: revisit this when we shake out more of the future DDK protocol.
    if (cache_policy == mapping_cache_flags_) {
        return MX_OK;
    }

    // If this VMO is mapped already it is not safe to allow its caching policy to change
    if (mapping_list_len_ != 0) {
        LTRACEF("Warning: trying to change cache policy while this vmo is mapped!\n");
        return MX_ERR_BAD_STATE;
    }

    mapping_cache_flags_ = cache_policy;
    return MX_OK;
}
