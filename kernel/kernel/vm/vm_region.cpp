// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/vm/vm_region.h>

#include "vm_priv.h"
#include <assert.h>
#include <err.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <string.h>
#include <trace.h>
#include <utils/type_support.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

VmRegion::VmRegion(VmAspace& aspace, vaddr_t base, size_t size, uint arch_mmu_flags,
                   const char* name)
    : base_(base), size_(size), arch_mmu_flags_(arch_mmu_flags), aspace_(&aspace) {
    strlcpy(name_, name, sizeof(name_));
    LTRACEF("%p '%s'\n", this, name_);
}

utils::RefPtr<VmRegion> VmRegion::Create(VmAspace& aspace, vaddr_t base, size_t size,
                                         uint arch_mmu_flags, const char* name) {
    return utils::AdoptRef(new VmRegion(aspace, base, size, arch_mmu_flags, name));
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

    // detach from any object we have mapped
    object_.reset();

    return NO_ERROR;
}

void VmRegion::Dump() const {
    DEBUG_ASSERT(magic_ == MAGIC);
    printf(
        "\tregion %p: ref %d name '%s' range 0x%lx - 0x%lx size 0x%zx mmu_flags 0x%x "
        "vmo %p offset 0x%llx\n",
        this, ref_count_debug(), name_, base_, base_ + size_ - 1, size_, arch_mmu_flags_,
        object_.get(), object_offset_);
    if (object_.get()) {
        object_->Dump();
    }
}

status_t VmRegion::Protect(uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    arch_mmu_flags_ = arch_mmu_flags;

    auto err = arch_mmu_protect(&aspace_->arch_aspace(), base_, size_ / PAGE_SIZE, arch_mmu_flags);
    LTRACEF("arch_mmu_protect returns %d\n", err);
    // TODO: deal with error mapping here

    return NO_ERROR;
}

int VmRegion::Unmap() {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p '%s'\n", this, name_);

    // unmap the section of address space we cover
    return arch_mmu_unmap(&aspace_->arch_aspace(), base_, size_ / PAGE_SIZE);
}

status_t VmRegion::SetObject(utils::RefPtr<VmObject> o, uint64_t offset) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p '%s', o %p, offset 0x%llx\n", this, name_, &o, offset);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));

    DEBUG_ASSERT(!object_); // cant handle setting a different object than before right now

    object_ = o;
    object_offset_ = offset;

    return NO_ERROR;
}

status_t VmRegion::MapPhysicalRange(size_t offset, size_t len, paddr_t paddr, bool allow_remap) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p '%s', offset 0x%zu, size 0x%zx, paddr 0x%lx, remap %d\n", this, name_, offset, len,
            paddr, allow_remap);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

    DEBUG_ASSERT(!object_); // assert on this for now

    if (!TrimRange(offset, len, size_))
        return ERR_INVALID_ARGS;

    if (object_) {
        // we have a backing object, which means we should not be physically mapping this
        return ERR_NO_MEMORY;
    }

    if (allow_remap) {
        auto ret = arch_mmu_unmap(&aspace_->arch_aspace(), base_ + offset, len / PAGE_SIZE);
        if (ret < 0) {
            TRACEF("error unmapping old region\n");
            return ret;
        }
    }

    auto ret = arch_mmu_map(&aspace_->arch_aspace(), base_ + offset, paddr, len / PAGE_SIZE,
                            arch_mmu_flags_);
    if (ret < 0) {
        TRACEF("error %d mapping pages at va 0x%lx pa 0x%lx\n", ret, base_, paddr);
    }
    return (ret < 0) ? ret : 0;
}

status_t VmRegion::MapRange(size_t offset, size_t len, bool commit) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("region %p '%s', offset 0x%zu, size 0x%zx\n", this, name_, offset, len);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

    DEBUG_ASSERT(object_); // assert on this for now

    if (!object_) {
        // we have no backing object, you should not call MapRange on this
        return ERR_NO_MEMORY;
    }

    // iterate through the range, grabbing a page from the underlying object and mapping it in
    size_t o;
    for (o = offset; o < offset + len; o += PAGE_SIZE) {
        uint64_t vmo_offset = object_offset_ + o;
        vm_page_t* p = object_->GetPage(vmo_offset);
        if (!p) {
            if (!commit) {
                // no page to map, skip ahead
                continue;
            }

            int64_t committed = object_->CommitRange(vmo_offset, PAGE_SIZE);
            if (committed < 0 || committed != PAGE_SIZE) {
                LTRACEF("error committing memory for region\n");
                return (status_t)committed;
            }

            p = object_->GetPage(vmo_offset);
        }

        DEBUG_ASSERT(p);

        vaddr_t va = base_ + o;
        paddr_t pa = vm_page_to_paddr(p);
        LTRACEF_LEVEL(2, "mapping pa 0x%lx to va 0x%lx\n", pa, va);

        auto ret = arch_mmu_map(&aspace_->arch_aspace(), va, pa, 1, arch_mmu_flags_);
        if (ret < 0) {
            TRACEF("error %d mapping page at va 0x%lx pa 0x%lx\n", ret, va, pa);
        }
    }

    return NO_ERROR;
}

status_t VmRegion::PageFault(vaddr_t va, uint pf_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(va >= base_ && va <= base_ + size_ - 1);

    va = ROUNDDOWN(va, PAGE_SIZE);
    uint64_t vmo_offset = va - base_ + object_offset_;

    LTRACEF("%p '%s', vmo_offset 0x%llx, pf_flags 0x%x\n", this, name_, vmo_offset, pf_flags);

    // make sure we have permission to continue
    if ((pf_flags & VMM_PF_FLAG_USER) && (arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_USER) == 0) {
        // user page fault on non user mapped region
        LTRACEF("permission failure: user fault on non user region\n");
        return ERR_ACCESS_DENIED;
    }
    if ((pf_flags & VMM_PF_FLAG_WRITE) && (arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_RO)) {
        // write to a read only region
        LTRACEF("permission failure: write fault on read only region\n");
        return ERR_ACCESS_DENIED;
    }
    if ((pf_flags & VMM_PF_FLAG_INSTRUCTION) && (arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_NO_EXECUTE)) {
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

    if (!object_) {
        // we have no backing object, error
        TRACEF("ERROR: faulted on region with no backing object\n");
        return ERR_NO_MEMORY;
    }

    // fault in or grab an existing page
    vm_page_t* new_p = object_->FaultPage(vmo_offset, pf_flags);
    if (!new_p) {
        TRACEF("ERROR: failed to fault in or grab existing page\n");
        return ERR_NO_MEMORY;
    }
    paddr_t new_pa = vm_page_to_paddr(new_p);

    // see if something is mapped here now
    // this may happen if we are one of multiple threads racing on a single address
    uint page_flags;
    paddr_t pa;
    status_t err = arch_mmu_query(&aspace_->arch_aspace(), va, &pa, &page_flags);
    if (err >= 0) {
        LTRACEF("queried va, page at pa 0x%lx, flags 0x%x is already there\n", pa, page_flags);
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
            printf("KERN: thread %s faulted on va 0x%lx, different page was present, unhandled\n",
                    get_current_thread()->name, va);
            return ERR_NOT_IMPLEMENTED;
        }
    } else {
        // nothing was mapped there before, map it now
        LTRACEF("mapping pa 0x%lx to va 0x%lx\n", new_pa, va);
        auto ret = arch_mmu_map(&aspace_->arch_aspace(), va, new_pa, 1, arch_mmu_flags_);
        if (ret < 0) {
            TRACEF("failed to map page\n");
            return ERR_NO_MEMORY;
        }
    }

    return NO_ERROR;
}
