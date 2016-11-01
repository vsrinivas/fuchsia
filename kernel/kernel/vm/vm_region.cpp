// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/vm/vm_region.h>

#include "vm_priv.h"
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <mxtl/auto_lock.h>
#include <mxtl/type_support.h>
#include <new.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

VmRegion::VmRegion(VmAspace& aspace, vaddr_t base, size_t size,
                   mxtl::RefPtr<VmObject> vmo, uint64_t offset,
                   uint arch_mmu_flags, const char* name)
    : base_(base), size_(size), arch_mmu_flags_(arch_mmu_flags), aspace_(&aspace),
      object_(mxtl::move(vmo)), object_offset_(offset), vmo_lock_(object_->lock()) {
    strlcpy(name_, name, sizeof(name_));
    LTRACEF("%p '%s'\n", this, name_);

    AutoLock al(vmo_lock_);
    object_->AddRegionLocked(this);
}

mxtl::RefPtr<VmRegion> VmRegion::Create(VmAspace& aspace, vaddr_t base, size_t size,
                                        mxtl::RefPtr<VmObject> vmo, uint64_t offset,
                                        uint arch_mmu_flags, const char* name) {
    AllocChecker ac;
    auto r = mxtl::AdoptRef(new (&ac) VmRegion(aspace, base, size, mxtl::move(vmo), offset, arch_mmu_flags, name));
    return ac.check() ? r : nullptr;
}

VmRegion::~VmRegion() {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p '%s'\n", this, name_);

    Destroy();

    // clear the magic
    magic_ = 0;
}

status_t VmRegion::Destroy() {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p '%s'\n", this, name_);

    if (object_) {
        {
            AutoLock al(vmo_lock_);
            object_->RemoveRegionLocked(this);
        }

        // detach from any object we have mapped
        object_.reset();
    }

    return NO_ERROR;
}

void VmRegion::Dump() const {
    DEBUG_ASSERT(magic_ == MAGIC);
    printf(
        "\tregion %p: ref %d name '%s' range %#" PRIxPTR " - %#" PRIxPTR
        " size %#zx mmu_flags %#x vmo %p offset %#" PRIx64 "\n",
        this, ref_count_debug(), name_, base_, base_ + size_ - 1, size_,
        arch_mmu_flags_, object_.get(), object_offset_);
    object_->Dump();
}

size_t VmRegion::AllocatedPages() const {
    DEBUG_ASSERT(magic_ == MAGIC);
    return object_->AllocatedPages();
}

status_t VmRegion::Protect(uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);

    // grab the lock for the vmo
    AutoLock al(vmo_lock_);

    arch_mmu_flags_ = arch_mmu_flags;

    auto err = arch_mmu_protect(&aspace_->arch_aspace(), base_, size_ / PAGE_SIZE, arch_mmu_flags);
    LTRACEF("arch_mmu_protect returns %d\n", err);
    // TODO: deal with error mapping here

    return NO_ERROR;
}

int VmRegion::Unmap() {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p '%s'\n", this, name_);

    // grab the lock for the vmo
    AutoLock al(vmo_lock_);

    // unmap the section of address space we cover
    return arch_mmu_unmap(&aspace_->arch_aspace(), base_, size_ / PAGE_SIZE);
}

status_t VmRegion::MapRange(size_t offset, size_t len, bool commit) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("region %p '%s', offset 0x%zu, size 0x%zx\n", this, name_, offset, len);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

    // grab the lock for the vmo
    AutoLock al(vmo_lock_);

    // iterate through the range, grabbing a page from the underlying object and mapping it in
    size_t o;
    for (o = offset; o < offset + len; o += PAGE_SIZE) {
        uint64_t vmo_offset = object_offset_ + o;

        status_t status;
        paddr_t pa;
        if (commit) {
            status = object_->FaultPageLocked(vmo_offset, VMM_PF_FLAG_WRITE, &pa);
        } else {
            status = object_->GetPageLocked(vmo_offset, &pa);
        }
        if (status < 0) {
            // no page to map, skip ahead
            continue;
        }

        vaddr_t va = base_ + o;
        LTRACEF_LEVEL(2, "mapping pa %#" PRIxPTR " to va %#" PRIxPTR "\n",
                      pa, va);

        auto ret = arch_mmu_map(&aspace_->arch_aspace(), va, pa, 1, arch_mmu_flags_);
        if (ret < 0) {
            TRACEF("error %d mapping page at va %#" PRIxPTR " pa %#" PRIxPTR
                   "\n",
                   ret, va, pa);
        }
    }

    return NO_ERROR;
}

status_t VmRegion::PageFault(vaddr_t va, uint pf_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(va >= base_ && va <= base_ + size_ - 1);

    va = ROUNDDOWN(va, PAGE_SIZE);
    uint64_t vmo_offset = va - base_ + object_offset_;

    LTRACEF("%p '%s', vmo_offset %#" PRIx64 ", pf_flags %#x\n",
            this, name_, vmo_offset, pf_flags);

    // make sure we have permission to continue
    if ((pf_flags & VMM_PF_FLAG_USER) && (arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_USER) == 0) {
        // user page fault on non user mapped region
        LTRACEF("permission failure: user fault on non user region\n");
        return ERR_ACCESS_DENIED;
    }
    if ((pf_flags & VMM_PF_FLAG_WRITE) && !(arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_WRITE)) {
        // write to a non-writeable region
        LTRACEF("permission failure: write fault on non-writable region\n");
        return ERR_ACCESS_DENIED;
    }
    if ((pf_flags & VMM_PF_FLAG_INSTRUCTION) && !(arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_EXECUTE)) {
        // instruction fetch from a no execute region
        LTRACEF("permission failure: execute fault on no execute region\n");
        return ERR_ACCESS_DENIED;
    }

    if (!(pf_flags & VMM_PF_FLAG_NOT_PRESENT)) {
        // kernel attempting to access userspace, and permissions were fine, so
        // architecture prevented the cross-privilege access
        if (!(pf_flags & VMM_PF_FLAG_USER) && aspace_->is_user()) {
            TRACEF("ERROR: kernel faulted on user address\n");
            return ERR_ACCESS_DENIED;
        }
    }

    // grab the lock for the vmo
    AutoLock al(vmo_lock_);

    // fault in or grab an existing page
    paddr_t new_pa;
    auto status = object_->FaultPageLocked(vmo_offset, pf_flags, &new_pa);
    if (status < 0) {
        TRACEF("ERROR: failed to fault in or grab existing page\n");
        return status;
    }

    // see if something is mapped here now
    // this may happen if we are one of multiple threads racing on a single address
    uint page_flags;
    paddr_t pa;
    status_t err = arch_mmu_query(&aspace_->arch_aspace(), va, &pa, &page_flags);
    if (err >= 0) {
        LTRACEF("queried va, page at pa %#" PRIxPTR ", flags %#x is already there\n", pa, page_flags);
        if (pa == new_pa) {
            // page was already mapped, are the permissions compatible?
            if (page_flags == arch_mmu_flags_)
                return NO_ERROR;

            // same page, different permission
            auto ret = arch_mmu_protect(&aspace_->arch_aspace(), va, 1, arch_mmu_flags_);
            if (ret < 0) {
                TRACEF("failed to modify permissions on existing mapping\n");
                return ERR_NO_MEMORY;
            }
        } else {
            // some other page is mapped there already
            // currently this is an error situation, since there's no way for this to have
            // happened, but in the future this will be a path for copy-on-write.
            // TODO: implement
            printf("KERN: thread %s faulted on va %#" PRIxPTR ", different page was present, unhandled\n",
                   get_current_thread()->name, va);
            return ERR_NOT_SUPPORTED;
        }
    } else {
        // nothing was mapped there before, map it now
        LTRACEF("mapping pa %#" PRIxPTR " to va %#" PRIxPTR "\n", new_pa, va);
        auto ret = arch_mmu_map(&aspace_->arch_aspace(), va, new_pa, 1, arch_mmu_flags_);
        if (ret < 0) {
            TRACEF("failed to map page\n");
            return ERR_NO_MEMORY;
        }
    }

    // TODO: figure out what to do with this
#if ARCH_ARM64
    if (arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_EXECUTE)
        arch_sync_cache_range(va, PAGE_SIZE);
#endif
    return NO_ERROR;
}
