// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/vm_object_physical.h"

#include "vm_priv.h"

#include <assert.h>
#include <err.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <inttypes.h>
#include <lib/console.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <vm/vm.h>
#include <zircon/types.h>

using fbl::AutoLock;

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

VmObjectPhysical::VmObjectPhysical(paddr_t base, uint64_t size)
    : size_(size), base_(base) {
    LTRACEF("%p, size %#" PRIx64 "\n", this, size_);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(size_));
}

VmObjectPhysical::~VmObjectPhysical() {
    canary_.Assert();
    LTRACEF("%p\n", this);
}

zx_status_t VmObjectPhysical::Create(paddr_t base, uint64_t size, fbl::RefPtr<VmObject>* obj) {
    if (!IS_PAGE_ALIGNED(base) || !IS_PAGE_ALIGNED(size) || size == 0)
        return ZX_ERR_INVALID_ARGS;

    // check that base + size is a valid range
    paddr_t safe_base;
    if (add_overflow(base, size - 1, &safe_base)) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    auto vmo = fbl::AdoptRef<VmObject>(new (&ac) VmObjectPhysical(base, size));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    // Physical VMOs should default to uncached access.
    vmo->SetMappingCachePolicy(ARCH_MMU_FLAG_UNCACHED);

    *obj = fbl::move(vmo);

    return ZX_OK;
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
zx_status_t VmObjectPhysical::GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                                            vm_page_t** _page, paddr_t* _pa) {
    canary_.Assert();

    if (_page)
        *_page = nullptr;

    if (offset >= size_)
        return ZX_ERR_OUT_OF_RANGE;

    uint64_t pa = base_ + ROUNDDOWN(offset, PAGE_SIZE);
    if (pa > UINTPTR_MAX)
        return ZX_ERR_OUT_OF_RANGE;

    *_pa = (paddr_t)pa;

    return ZX_OK;
}

zx_status_t VmObjectPhysical::LookupUser(uint64_t offset, uint64_t len, user_inout_ptr<paddr_t> buffer,
                                         size_t buffer_size) {
    canary_.Assert();

    if (unlikely(len == 0))
        return ZX_ERR_INVALID_ARGS;

    AutoLock a(&lock_);

    // verify that the range is within the object
    if (unlikely(!InRange(offset, len, size_)))
        return ZX_ERR_OUT_OF_RANGE;

    uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    uint64_t end = offset + len;
    uint64_t end_page_offset = ROUNDUP(end, PAGE_SIZE);

    // compute the size of the table we'll need and make sure it fits in the user buffer
    uint64_t table_size = ((end_page_offset - start_page_offset) / PAGE_SIZE) * sizeof(paddr_t);
    if (unlikely(table_size > buffer_size))
        return ZX_ERR_BUFFER_TOO_SMALL;

    size_t index = 0;
    for (uint64_t off = start_page_offset; off != end_page_offset; off += PAGE_SIZE, index++) {
        // find the physical address
        uint64_t tmp = base_ + off;
        if (tmp > UINTPTR_MAX)
            return ZX_ERR_OUT_OF_RANGE;

        paddr_t pa = (paddr_t)tmp;

        // check that we didn't wrap
        DEBUG_ASSERT(pa >= base_);

        // copy it out into user space
        auto status = buffer.element_offset(index).copy_to_user(pa);
        if (unlikely(status < 0))
            return status;
    }

    return ZX_OK;
}

zx_status_t VmObjectPhysical::Lookup(uint64_t offset, uint64_t len, uint pf_flags,
                                     vmo_lookup_fn_t lookup_fn, void* context) {
    canary_.Assert();

    if (unlikely(len == 0))
        return ZX_ERR_INVALID_ARGS;

    AutoLock a(&lock_);
    if (unlikely(!InRange(offset, len, size_)))
        return ZX_ERR_OUT_OF_RANGE;

    uint64_t cur_offset = ROUNDDOWN(offset, PAGE_SIZE);
    uint64_t end = offset + len;
    uint64_t end_page_offset = ROUNDUP(end, PAGE_SIZE);

    for (size_t idx = 0; cur_offset < end_page_offset; cur_offset += PAGE_SIZE, ++idx) {
        zx_status_t status = lookup_fn(context, cur_offset, idx, base_ + cur_offset);
        if (status != ZX_OK) {
            return status;
        }
    }
    return ZX_OK;
}

zx_status_t VmObjectPhysical::GetMappingCachePolicy(uint32_t* cache_policy) {
    AutoLock l(&lock_);

    if (!cache_policy) {
        return ZX_ERR_INVALID_ARGS;
    }

    *cache_policy = mapping_cache_flags_;
    return ZX_OK;
}

zx_status_t VmObjectPhysical::SetMappingCachePolicy(const uint32_t cache_policy) {
    // Is it a valid cache flag?
    if (cache_policy & ~ZX_CACHE_POLICY_MASK) {
        return ZX_ERR_INVALID_ARGS;
    }

    AutoLock l(&lock_);

    // If the cache policy is already configured on this VMO and matches
    // the requested policy then this is a no-op. This is a common practice
    // in the serialio and magma drivers, but may change.
    // TODO: revisit this when we shake out more of the future DDK protocol.
    if (cache_policy == mapping_cache_flags_) {
        return ZX_OK;
    }

    // If this VMO is mapped already it is not safe to allow its caching policy to change
    if (mapping_list_len_ != 0) {
        LTRACEF("Warning: trying to change cache policy while this vmo is mapped!\n");
        return ZX_ERR_BAD_STATE;
    }

    mapping_cache_flags_ = cache_policy;
    return ZX_OK;
}
