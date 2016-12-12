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

// Maximum size of allocation gap that can be requested
#define MAX_MIN_ALLOC_GAP (PAGE_SIZE * 1024)

VmAddressRegion::VmAddressRegion(VmAspace& aspace, vaddr_t base, size_t size, uint32_t vmar_flags)
    : VmAddressRegionOrMapping(kMagic, base, size, vmar_flags | VMAR_CAN_RWX_FLAGS,
                               aspace, nullptr, "root") {

    // We add in CAN_RWX_FLAGS above, since an address space can't usefully
    // contain a process without all of these.

    LTRACEF("%p '%s'\n", this, name_);
}

VmAddressRegion::VmAddressRegion(VmAddressRegion& parent, vaddr_t base, size_t size,
                                 uint32_t vmar_flags, const char* name)
    : VmAddressRegionOrMapping(kMagic, base, size, vmar_flags, *parent.aspace_, &parent, name) {

    LTRACEF("%p '%s'\n", this, name_);
}

VmAddressRegion::VmAddressRegion(VmAspace& kernel_aspace)
    : VmAddressRegion(kernel_aspace, kernel_aspace.base(), kernel_aspace.size(),
                      VMAR_FLAG_CAN_MAP_SPECIFIC | VMAR_FLAG_COMPACT) {

    // Activate the kernel root aspace immediately
    state_ = LifeCycleState::ALIVE;
}

VmAddressRegion::~VmAddressRegion() {
    DEBUG_ASSERT(magic_ == kMagic);
}

status_t VmAddressRegion::CreateRoot(VmAspace& aspace, uint32_t vmar_flags,
                                     mxtl::RefPtr<VmAddressRegion>* out) {
    DEBUG_ASSERT(out);

    AllocChecker ac;
    auto vmar = new (&ac) VmAddressRegion(aspace, aspace.base(), aspace.size(), vmar_flags);
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }

    vmar->state_ = LifeCycleState::ALIVE;
    *out = mxtl::AdoptRef(vmar);
    return NO_ERROR;
}

status_t VmAddressRegion::CreateSubVmarInternal(size_t offset, size_t size, uint8_t align_pow2,
                                                uint32_t vmar_flags, mxtl::RefPtr<VmObject> vmo,
                                                uint64_t vmo_offset, uint arch_mmu_flags,
                                                const char* name,
                                                mxtl::RefPtr<VmAddressRegionOrMapping>* out) {
    DEBUG_ASSERT(out);

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ERR_BAD_STATE;
    }

    if (size == 0) {
        return ERR_INVALID_ARGS;
    }

    // Check if there are any RWX privileges that the child would have that the
    // parent does not.
    if (vmar_flags & ~flags_ & VMAR_CAN_RWX_FLAGS) {
        return ERR_ACCESS_DENIED;
    }

    bool is_specific = !!(vmar_flags & VMAR_FLAG_SPECIFIC);
    if (!is_specific && offset != 0) {
        return ERR_INVALID_ARGS;
    }

    // Check that we have the required privileges if we want a SPECIFIC mapping
    if (is_specific && !(flags_ & VMAR_FLAG_CAN_MAP_SPECIFIC)) {
        return ERR_ACCESS_DENIED;
    }

    if (offset >= size_ || size > size_ - offset) {
        return ERR_INVALID_ARGS;
    }

    vaddr_t new_base = base_ + offset;
    if (is_specific) {
        if (!IS_PAGE_ALIGNED(new_base)) {
            return ERR_INVALID_ARGS;
        }
        if (!IsRangeAvailableLocked(new_base, size)) {
            return ERR_NO_MEMORY;
        }
    } else {
        // TODO(teisenbe): This is a hack for the migration to the new interface.  It works
        // around the lack of VALLOC_BASE until things start using subregions.
        if (vmar_flags & VMAR_FLAG_MAP_HIGH) {
#if _LP64
            static const vaddr_t mmio_map_base_address = 0x7ff000000000ULL;
#else
            static const vaddr_t mmio_map_base_address = 0x20000000UL;
#endif
            new_base = mmio_map_base_address;
        }

        // If we're not mapping to a specific place, search for an opening.
        new_base = AllocSpotLocked(new_base, size, align_pow2, 0 /*alloc_gap*/, arch_mmu_flags);
        if (new_base == static_cast<vaddr_t>(-1)) {
            return ERR_NO_MEMORY;
        }
    }

    AllocChecker ac;
    mxtl::RefPtr<VmAddressRegionOrMapping> vmar;
    if (vmo) {
        vmar = mxtl::AdoptRef(new (&ac)
                              VmMapping(*this, new_base, size, vmar_flags,
                                        mxtl::move(vmo), vmo_offset, arch_mmu_flags, name));
    } else {
        vmar = mxtl::AdoptRef(new (&ac)
                              VmAddressRegion(*this, new_base, size, vmar_flags, name));
    }

    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }

    vmar->Activate();
    *out = mxtl::move(vmar);
    return NO_ERROR;
}

status_t VmAddressRegion::CreateSubVmar(size_t offset, size_t size, uint8_t align_pow2,
                                        uint32_t vmar_flags, const char* name,
                                        mxtl::RefPtr<VmAddressRegion>* out) {
    DEBUG_ASSERT(out);

    if (!IS_PAGE_ALIGNED(size)) {
        return ERR_INVALID_ARGS;
    }

    // Check that only allowed flags have been set
    if (vmar_flags & ~(VMAR_FLAG_SPECIFIC | VMAR_FLAG_CAN_MAP_SPECIFIC |
                       VMAR_FLAG_COMPACT | VMAR_CAN_RWX_FLAGS)) {
        return ERR_INVALID_ARGS;
    }

    mxtl::RefPtr<VmAddressRegionOrMapping> res;
    status_t status = CreateSubVmarInternal(offset, size, align_pow2, vmar_flags, nullptr, 0,
                                            ARCH_MMU_FLAG_INVALID, name, &res);
    if (status != NO_ERROR) {
        return status;
    }
    // TODO(teisenbe): optimize this
    *out = res->as_vm_address_region();
    return NO_ERROR;
}

status_t VmAddressRegion::CreateVmMapping(size_t mapping_offset, size_t size, uint8_t align_pow2,
                                          uint32_t vmar_flags, mxtl::RefPtr<VmObject> vmo,
                                          uint64_t vmo_offset, uint arch_mmu_flags, const char* name,
                                          mxtl::RefPtr<VmMapping>* out) {
    DEBUG_ASSERT(out);
    LTRACEF("%p %#zx %#zx %x\n", this, mapping_offset, size, vmar_flags);

    // Check that only allowed flags have been set
    if (vmar_flags & ~(VMAR_FLAG_SPECIFIC | VMAR_CAN_RWX_FLAGS | VMAR_FLAG_MAP_HIGH)) {
        return ERR_INVALID_ARGS;
    }

    // Validate that arch_mmu_flags does not contain any prohibited flags
    if (!is_valid_mapping_flags(arch_mmu_flags)) {
        return ERR_ACCESS_DENIED;
    }

    // If we're mapping it with a specific permission, we should allow
    // future Protect() calls on the mapping to keep that permission.
    if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_READ) {
        vmar_flags |= VMAR_FLAG_CAN_MAP_READ;
    }
    if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_WRITE) {
        vmar_flags |= VMAR_FLAG_CAN_MAP_WRITE;
    }
    if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
        vmar_flags |= VMAR_FLAG_CAN_MAP_EXECUTE;
    }

    size = ROUNDUP(size, PAGE_SIZE);

    mxtl::RefPtr<VmAddressRegionOrMapping> res;
    status_t status =
        CreateSubVmarInternal(mapping_offset, size, align_pow2, vmar_flags, mxtl::move(vmo),
                              vmo_offset, arch_mmu_flags, name, &res);
    if (status != NO_ERROR) {
        return status;
    }
    // TODO(teisenbe): optimize this
    *out = res->as_vm_mapping();
    return NO_ERROR;
}

status_t VmAddressRegion::DestroyLocked() {
    DEBUG_ASSERT(magic_ == kMagic);
    DEBUG_ASSERT(is_mutex_held(&aspace_->lock()));
    LTRACEF("%p '%s'\n", this, name_);

    // Take a reference to ourself, so that we do not get destructed after
    // dropping our last reference in this method (e.g. when calling
    // subregions_.erase below).
    mxtl::RefPtr<VmAddressRegion> self(this);

    while (!subregions_.is_empty()) {
        mxtl::RefPtr<VmAddressRegionOrMapping> child(&subregions_.front());

        // DestroyLocked should remove this child from our list on success
        status_t status = child->DestroyLocked();
        if (status != NO_ERROR) {
            // TODO(teisenbe): Do we want to handle this case differently?
            return status;
        }
    }

    // Detach the now dead region from the parent
    if (parent_) {
        DEBUG_ASSERT(subregion_list_node_.InContainer());
        parent_->RemoveSubregion(this);
    }

    parent_ = nullptr;
    state_ = LifeCycleState::DEAD;
    return NO_ERROR;
}

void VmAddressRegion::RemoveSubregion(VmAddressRegionOrMapping* region) {
    subregions_.erase(*region);
}

mxtl::RefPtr<VmAddressRegionOrMapping> VmAddressRegion::FindRegion(vaddr_t addr) {
    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return nullptr;
    }
    return FindRegionLocked(addr);
}

mxtl::RefPtr<VmAddressRegionOrMapping> VmAddressRegion::FindRegionLocked(vaddr_t addr) {
    DEBUG_ASSERT(magic_ == kMagic);

    // Find the first region with a base greather than *addr*.  If a region
    // exists for *addr*, it will be immediately before it.
    auto itr = --subregions_.upper_bound(addr);
    if (!itr.IsValid() || itr->base() > addr || addr > itr->base() + itr->size() - 1) {
        return nullptr;
    }

    return mxtl::RefPtr<VmAddressRegionOrMapping>(&*itr);
}

size_t VmAddressRegion::AllocatedPagesLocked() const {
    DEBUG_ASSERT(magic_ == kMagic);
    DEBUG_ASSERT(is_mutex_held(&aspace_->lock()));

    if (state_ != LifeCycleState::ALIVE) {
        return 0;
    }

    size_t sum = 0;
    for (const auto& child : subregions_) {
        sum += child.AllocatedPagesLocked();
    }
    return sum;
}

status_t VmAddressRegion::PageFault(vaddr_t va, uint pf_flags) {
    DEBUG_ASSERT(magic_ == kMagic);
    DEBUG_ASSERT(is_mutex_held(&aspace_->lock()));

    mxtl::RefPtr<VmAddressRegion> vmar(this);
    while (1) {
        mxtl::RefPtr<VmAddressRegionOrMapping> next(vmar->FindRegionLocked(va));
        if (!next) {
            return ERR_NOT_FOUND;
        }

        if (next->is_mapping()) {
            return next->PageFault(va, pf_flags);
        }

        vmar = next->as_vm_address_region();
    }
}

bool VmAddressRegion::IsRangeAvailableLocked(vaddr_t base, size_t size) {
    DEBUG_ASSERT(is_mutex_held(&aspace_->lock()));
    DEBUG_ASSERT(size > 0);

    // Find the first region with base > *base*.  Since subregions_ has no
    // overlapping elements, we just need to check this one and the prior
    // child.

    auto prev = subregions_.upper_bound(base);
    auto next = prev--;

    if (prev.IsValid()) {
        safeint::CheckedNumeric<vaddr_t> prev_last_byte = prev->base();
        prev_last_byte += prev->size() - 1;
        if (!prev_last_byte.IsValid() || prev_last_byte.ValueOrDie() >= base) {
            return false;
        }
    }

    if (next.IsValid() && next != subregions_.end()) {
        safeint::CheckedNumeric<vaddr_t> last_byte = base;
        last_byte += size - 1;
        if (!last_byte.IsValid() || next->base() <= last_byte.ValueOrDie()) {
            return false;
        }
    }
    return true;
}

bool VmAddressRegion::CheckGapLocked(const ChildList::iterator& prev,
                                     const ChildList::iterator& next,
                                     vaddr_t* pva, vaddr_t search_base, vaddr_t align,
                                     size_t region_size, size_t min_gap, uint arch_mmu_flags) {
    DEBUG_ASSERT(is_mutex_held(&aspace_->lock()));

    safeint::CheckedNumeric<vaddr_t> gap_beg; // first byte of a gap
    safeint::CheckedNumeric<vaddr_t> gap_end; // last byte of a gap
    vaddr_t real_gap_beg = 0;
    vaddr_t real_gap_end = 0;

    uint prev_arch_mmu_flags;
    uint next_arch_mmu_flags;

    DEBUG_ASSERT(pva);

    // compute the starting address of the gap
    if (prev.IsValid()) {
        gap_beg = prev->base();
        gap_beg += prev->size();
        gap_beg += min_gap;
    } else {
        gap_beg = base_;
    }

    if (!gap_beg.IsValid())
        goto not_found;
    real_gap_beg = gap_beg.ValueOrDie();

    // compute the ending address of the gap
    if (next.IsValid()) {
        if (real_gap_beg == next->base())
            goto next_gap; // no gap between regions
        gap_end = next->base();
        gap_end -= 1;
        gap_end -= min_gap;
    } else {
        if (real_gap_beg == base_ + size_)
            goto not_found; // no gap at the end of address space. Stop search
        gap_end = base_;
        gap_end += size_ - 1;
    }

    if (!gap_end.IsValid())
        goto not_found;
    real_gap_end = gap_end.ValueOrDie();

    DEBUG_ASSERT(real_gap_end > real_gap_beg);

    // trim it to the search range
    if (real_gap_end <= search_base)
        return false;
    if (real_gap_beg < search_base)
        real_gap_beg = search_base;

    DEBUG_ASSERT(real_gap_end > real_gap_beg);

    LTRACEF_LEVEL(2, "search base %#" PRIxPTR " real_gap_beg %#" PRIxPTR " end %#" PRIxPTR "\n",
                  search_base, real_gap_beg, real_gap_end);

    prev_arch_mmu_flags = (prev.IsValid() && prev->is_mapping())
                              ? prev->as_vm_mapping()->arch_mmu_flags()
                              : ARCH_MMU_FLAG_INVALID;

    next_arch_mmu_flags = (next.IsValid() && next->is_mapping())
                              ? next->as_vm_mapping()->arch_mmu_flags()
                              : ARCH_MMU_FLAG_INVALID;

    *pva =
        arch_mmu_pick_spot(&aspace_->arch_aspace(), real_gap_beg, prev_arch_mmu_flags, real_gap_end,
                           next_arch_mmu_flags, align, region_size, arch_mmu_flags);
    if (*pva < real_gap_beg)
        goto not_found; // address wrapped around

    if (*pva < real_gap_end && ((real_gap_end - *pva + 1) >= region_size)) {
        // we have enough room
        return true; // found spot, stop search
    }

next_gap:
    return false; // continue search

not_found:
    *pva = -1;
    return true; // not_found: stop search
}

vaddr_t VmAddressRegion::AllocSpotLocked(vaddr_t base, size_t size, uint8_t align_pow2,
                                   size_t min_alloc_gap, uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == kMagic);
    DEBUG_ASSERT(size > 0 && IS_PAGE_ALIGNED(size));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(min_alloc_gap));
    DEBUG_ASSERT(min_alloc_gap <= MAX_MIN_ALLOC_GAP);
    DEBUG_ASSERT(is_mutex_held(&aspace_->lock()));

    LTRACEF_LEVEL(2, "aspace %p base %#" PRIxPTR " size 0x%zx align %hhu\n", this, base, size,
                  align_pow2);

    if (align_pow2 < PAGE_SIZE_SHIFT)
        align_pow2 = PAGE_SIZE_SHIFT;
    vaddr_t align = 1UL << align_pow2;

    vaddr_t spot;

    // Find the first gap in the address space which can contain a region of the
    // requested size.
    auto before_iter = subregions_.end();
    auto after_iter = subregions_.begin();

    do {
        if (CheckGapLocked(before_iter, after_iter, &spot, base, align, size, min_alloc_gap,
                     arch_mmu_flags)) {
            return spot;
        }

        before_iter = after_iter++;
    } while (before_iter.IsValid());

    // couldn't find anything
    return -1;
}

void VmAddressRegion::Dump(uint depth) const {
    DEBUG_ASSERT(magic_ == kMagic);
    for (uint i = 0; i < depth; ++i) {
        printf("  ");
    }
    printf("region %p: ref %d name '%s' range %#" PRIxPTR " - %#" PRIxPTR " size %#zx\n", this,
           ref_count_debug(), name_, base_, base_ + size_ - 1, size_);
    for (const auto& child : subregions_) {
        child.Dump(depth + 1);
    }
}

void VmAddressRegion::Activate() {
    DEBUG_ASSERT(state_ == LifeCycleState::NOT_READY);
    DEBUG_ASSERT(is_mutex_held(&aspace_->lock()));

    state_ = LifeCycleState::ALIVE;
    parent_->subregions_.insert(mxtl::RefPtr<VmAddressRegionOrMapping>(this));
}
