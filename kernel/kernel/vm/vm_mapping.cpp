// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/vm/vm_address_region.h>

#include "vm_priv.h"
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <mxtl/auto_call.h>
#include <mxtl/auto_lock.h>
#include <new.h>
#include <safeint/safe_math.h>
#include <trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

VmMapping::VmMapping(VmAddressRegion& parent, vaddr_t base, size_t size, uint32_t vmar_flags,
                     mxtl::RefPtr<VmObject> vmo, uint64_t vmo_offset, uint arch_mmu_flags,
                     const char* name)
    : VmAddressRegionOrMapping(kMagic, base, size, vmar_flags, *parent.aspace_, &parent, name),
      object_(mxtl::move(vmo)), object_offset_(vmo_offset), arch_mmu_flags_(arch_mmu_flags) {

    LTRACEF("%p '%s'\n", this, name_);
}

VmMapping::~VmMapping() {
    DEBUG_ASSERT(magic_ == kMagic);
}

size_t VmMapping::AllocatedPages() const {
    DEBUG_ASSERT(magic_ == kMagic);

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ERR_BAD_STATE;
    }
    return object_->AllocatedPages();
}

void VmMapping::Dump(uint depth) const {
    DEBUG_ASSERT(magic_ == kMagic);
    for (uint i = 0; i < depth; ++i) {
        printf("  ");
    }
    printf("mapping %p: ref %d name '%s' range %#" PRIxPTR " - %#" PRIxPTR
           " size %#zx mmu_flags %#x vmo %p offset %#" PRIx64 "\n",
           this, ref_count_debug(), name_, base_, base_ + size_ - 1, size_, arch_mmu_flags_,
           object_.get(), object_offset_);
    object_->Dump(depth + 1);
}

status_t VmMapping::Protect(uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == kMagic);
    LTRACEF("%p %s %#" PRIxPTR " %#x %#x\n", this, name_, base_, flags_, arch_mmu_flags);

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ERR_BAD_STATE;
    }

    // Do not allow changing caching
    if (arch_mmu_flags & ARCH_MMU_FLAG_CACHE_MASK) {
        return ERR_INVALID_ARGS;
    }

    if (!is_valid_mapping_flags(arch_mmu_flags)) {
        return ERR_ACCESS_DENIED;
    }

    DEBUG_ASSERT(object_);
    // grab the lock for the vmo
    AutoLock al(object_->lock());

    // Persist our current caching mode
    arch_mmu_flags_ = arch_mmu_flags | (arch_mmu_flags_ & ARCH_MMU_FLAG_CACHE_MASK);

    auto err = arch_mmu_protect(&aspace_->arch_aspace(), base_, size_ / PAGE_SIZE, arch_mmu_flags_);
    LTRACEF("arch_mmu_protect returns %d\n", err);
    // TODO: deal with error mapping here

    return NO_ERROR;
}

status_t VmMapping::Unmap() {
    return Destroy();
}

status_t VmMapping::UnmapLocked() {
    DEBUG_ASSERT(magic_ == kMagic);
    DEBUG_ASSERT(is_mutex_held(&aspace_->lock()));

    if (state_ != LifeCycleState::ALIVE) {
        return ERR_BAD_STATE;
    }

    LTRACEF("%p '%s'\n", this, name_);

    // grab the lock for the vmo
    DEBUG_ASSERT(object_);
    AutoLock al(object_->lock());

    // unmap the section of address space we cover
    status_t status = arch_mmu_unmap(&aspace_->arch_aspace(), base_, size_ / PAGE_SIZE);
    if (status < 0) {
        return status;
    }
    return NO_ERROR;
}

status_t VmMapping::UnmapVmoRangeLocked(uint64_t offset, uint64_t len) {
    DEBUG_ASSERT(magic_ == kMagic);

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ERR_BAD_STATE;
    }

    LTRACEF("region %p '%s', offset %#" PRIx64 ", len %#" PRIx64 "\n", this, name_, offset, len);

    DEBUG_ASSERT(object_);
    DEBUG_ASSERT(object_->lock().IsHeld());

    DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(len));
    DEBUG_ASSERT(len > 0);

    if (len == 0)
        return NO_ERROR;

    // compute the intersection of the passed in vmo range and our mapping
    uint64_t offset_new;
    uint64_t len_new;
    if (!GetIntersect(object_offset_, static_cast<uint64_t>(size_), offset, len, offset_new,
                      len_new))
        return NO_ERROR;

    DEBUG_ASSERT(len_new <= SIZE_MAX);

    LTRACEF("intersection offset %#" PRIx64 ", len %#" PRIx64 "\n", offset_new, len_new);

    // make sure the base + offset is within our address space
    // should be, according to the range stored in base_ + size_
    safeint::CheckedNumeric<vaddr_t> unmap_base = base_;
    unmap_base += offset_new;

    LTRACEF("going to unmap %#" PRIxPTR ", len %#" PRIx64 "\n", unmap_base.ValueOrDie(), len_new);

    status_t status = arch_mmu_unmap(&aspace_->arch_aspace(), unmap_base.ValueOrDie(),
                                     static_cast<size_t>(len_new));
    if (status < 0)
        return status;

    return NO_ERROR;
}

status_t VmMapping::MapRange(size_t offset, size_t len, bool commit) {
    DEBUG_ASSERT(magic_ == kMagic);

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ERR_BAD_STATE;
    }

    LTRACEF("region %p '%s', offset 0x%zu, size 0x%zx\n", this, name_, offset, len);

    DEBUG_ASSERT(object_);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

    // grab the lock for the vmo
    AutoLock al(object_->lock());

    // iterate through the range, grabbing a page from the underlying object and
    // mapping it in
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
        LTRACEF_LEVEL(2, "mapping pa %#" PRIxPTR " to va %#" PRIxPTR "\n", pa, va);

        auto ret = arch_mmu_map(&aspace_->arch_aspace(), va, pa, 1, arch_mmu_flags_);
        if (ret < 0) {
            TRACEF("error %d mapping page at va %#" PRIxPTR " pa %#" PRIxPTR "\n", ret, va, pa);
        }
    }

    return NO_ERROR;
}

status_t VmMapping::DestroyLocked() {
    DEBUG_ASSERT(magic_ == kMagic);
    DEBUG_ASSERT(is_mutex_held(&aspace_->lock()));
    LTRACEF("%p '%s'\n", this, name_);

    // Take a reference to ourself, so that we do not get destructed after
    // dropping our last reference in this method (e.g. when calling
    // subregions_.erase below).
    mxtl::RefPtr<VmMapping> self(this);

    status_t status = UnmapLocked();
    if (status != NO_ERROR) {
        return status;
    }

    {
        AutoLock al(object_->lock());
        object_->RemoveRegionLocked(this);
    }

    // detach from any object we have mapped
    object_.reset();

    // Detach the now dead region from the parent
    if (parent_) {
        DEBUG_ASSERT(subregion_list_node_.InContainer());
        parent_->RemoveSubregion(this);
    }

    parent_ = nullptr;
    state_ = LifeCycleState::DEAD;
    return NO_ERROR;
}

status_t VmMapping::PageFault(vaddr_t va, uint pf_flags) {
    DEBUG_ASSERT(magic_ == kMagic);
    DEBUG_ASSERT(is_mutex_held(&aspace_->lock()));

    DEBUG_ASSERT(va >= base_ && va <= base_ + size_ - 1);

    va = ROUNDDOWN(va, PAGE_SIZE);
    uint64_t vmo_offset = va - base_ + object_offset_;

    LTRACEF("%p '%s', vmo_offset %#" PRIx64 ", pf_flags %#x\n", this, name_, vmo_offset, pf_flags);

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
    AutoLock al(object_->lock());

    // fault in or grab an existing page
    paddr_t new_pa;
    auto status = object_->FaultPageLocked(vmo_offset, pf_flags, &new_pa);
    if (status < 0) {
        TRACEF("ERROR: failed to fault in or grab existing page\n");
        TRACEF("%p '%s', vmo_offset %#" PRIx64 ", pf_flags %#x\n", this, name_, vmo_offset, pf_flags);
        return status;
    }

    // see if something is mapped here now
    // this may happen if we are one of multiple threads racing on a single
    // address
    uint page_flags;
    paddr_t pa;
    status_t err = arch_mmu_query(&aspace_->arch_aspace(), va, &pa, &page_flags);
    if (err >= 0) {
        LTRACEF("queried va, page at pa %#" PRIxPTR ", flags %#x is already there\n", pa,
                page_flags);
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
            // currently this is an error situation, since there's no way for this to
            // have
            // happened, but in the future this will be a path for copy-on-write.
            // TODO: implement
            printf("KERN: thread %s faulted on va %#" PRIxPTR
                   ", different page was present, unhandled\n",
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

void VmMapping::ActivateLocked() {
    DEBUG_ASSERT(state_ == LifeCycleState::NOT_READY);
    DEBUG_ASSERT(is_mutex_held(&aspace_->lock()));
    DEBUG_ASSERT(object_->lock().IsHeld());
    DEBUG_ASSERT(parent_);

    state_ = LifeCycleState::ALIVE;
    object_->AddRegionLocked(this);
    parent_->subregions_.insert(mxtl::RefPtr<VmAddressRegionOrMapping>(this));
}

void VmMapping::Activate() {
    AutoLock guard(object_->lock());
    ActivateLocked();
}
