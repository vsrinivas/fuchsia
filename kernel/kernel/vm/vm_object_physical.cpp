// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/vm/vm_object.h"

#include "vm_priv.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/auto_lock.h>
#include <kernel/vm.h>
#include <lib/console.h>
#include <lib/user_copy.h>
#include <new.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

VmObjectPhysical::VmObjectPhysical(paddr_t base, uint64_t size)
    : size_(size), base_(base) {
    LTRACEF("%p\n", this);

    // assert that base and size are reasonable
    DEBUG_ASSERT(base + size >= base);
}

VmObjectPhysical::~VmObjectPhysical() {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p\n", this);
}

mxtl::RefPtr<VmObject> VmObjectPhysical::Create(paddr_t base, uint64_t size) {
    if (!IS_PAGE_ALIGNED(base) || !IS_PAGE_ALIGNED(size) || size == 0)
        return nullptr;

    if (base + size < base)
        return nullptr;

    AllocChecker ac;
    auto vmo = mxtl::AdoptRef<VmObject>(new (&ac) VmObjectPhysical(base, size));
    if (!ac.check())
        return nullptr;

    return vmo;
}

void VmObjectPhysical::Dump(bool page_dump) {
    if (magic_ != MAGIC) {
        printf("VmObjectPhysical at %p has bad magic\n", this);
        return;
    }

    printf("\t\tobject %p: ref %d base %#" PRIxPTR " size %#" PRIx64 "\n",
           this, ref_count_debug(), base_, size_);
}

// get the physical address of a page at offset
status_t VmObjectPhysical::GetPageLocked(uint64_t offset, paddr_t* _pa) {
    DEBUG_ASSERT(lock_.IsHeld());

    if (offset >= size_)
        return ERR_OUT_OF_RANGE;

    uint64_t pa = base_ + ROUNDDOWN(offset, PAGE_SIZE);
    if (pa > UINTPTR_MAX)
        return ERR_OUT_OF_RANGE;

    *_pa = (paddr_t)pa;
    return NO_ERROR;
}

// get the physical address of a page at offset
status_t VmObjectPhysical::FaultPageLocked(uint64_t offset, uint pf_flags, paddr_t* _pa) {
    DEBUG_ASSERT(lock_.IsHeld());

    if (offset >= size_)
        return ERR_OUT_OF_RANGE;

    uint64_t pa = base_ + ROUNDDOWN(offset, PAGE_SIZE);
    if (pa > UINTPTR_MAX)
        return ERR_OUT_OF_RANGE;

    *_pa = (paddr_t)pa;
    return NO_ERROR;
}

status_t VmObjectPhysical::Lookup(uint64_t offset, uint64_t len, user_ptr<paddr_t> buffer, size_t buffer_size) {
    DEBUG_ASSERT(magic_ == MAGIC);

    if (unlikely(len == 0))
        return ERR_INVALID_ARGS;

    AutoLock a(lock_);

    // verify that the range is within the object
    if (unlikely(!InRange(offset, len, size_)))
        return ERR_OUT_OF_RANGE;

    uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    uint64_t end = offset + len;
    uint64_t end_page_offset = ROUNDUP(end, PAGE_SIZE);

    // compute the size of the table we'll need and make sure it fits in the user buffer
    uint64_t table_size = ((end_page_offset - start_page_offset) / PAGE_SIZE) * sizeof(paddr_t);
    if (unlikely(table_size > buffer_size))
        return ERR_BUFFER_TOO_SMALL;

    size_t index = 0;
    for (uint64_t off = start_page_offset; off != end_page_offset; off += PAGE_SIZE, index++) {
        // find the physical address
        uint64_t tmp = base_ + off;
        if (tmp > UINTPTR_MAX)
            return ERR_OUT_OF_RANGE;

        paddr_t pa = (paddr_t)tmp;

        // check that we didn't wrap
        DEBUG_ASSERT(pa >= base_);

        // copy it out into user space
        auto status = buffer.element_offset(index).copy_to_user(pa);
        if (unlikely(status < 0))
            return status;
    }

    return NO_ERROR;
}
