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
#include <mxtl/auto_lock.h>
#include <new.h>
#include <pow2.h>
#include <safeint/safe_math.h>
#include <trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)


VmAddressRegion::VmAddressRegion(VmAspace& aspace, vaddr_t base, size_t size, uint32_t vmar_flags)
    : VmAddressRegionOrMapping(kMagic, base, size, vmar_flags | VMAR_CAN_RWX_FLAGS,
                               &aspace, nullptr, "root") {

    // We add in CAN_RWX_FLAGS above, since an address space can't usefully
    // contain a process without all of these.

    LTRACEF("%p '%s'\n", this, name_);
}

VmAddressRegion::VmAddressRegion(VmAddressRegion& parent, vaddr_t base, size_t size,
                                 uint32_t vmar_flags, const char* name)
    : VmAddressRegionOrMapping(kMagic, base, size, vmar_flags, parent.aspace_.get(), &parent,
                               name) {

    LTRACEF("%p '%s'\n", this, name_);
}

VmAddressRegion::VmAddressRegion(VmAspace& kernel_aspace)
    : VmAddressRegion(kernel_aspace, kernel_aspace.base(), kernel_aspace.size(),
                      VMAR_FLAG_CAN_MAP_SPECIFIC | VMAR_FLAG_COMPACT) {

    // Activate the kernel root aspace immediately
    state_ = LifeCycleState::ALIVE;
}

VmAddressRegion::VmAddressRegion()
    : VmAddressRegionOrMapping(kMagic, 0, 0, 0, nullptr, nullptr, "dummy") {

    LTRACEF("%p '%s'\n", this, name_);
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

    bool is_specific_overwrite  = static_cast<bool>(vmar_flags & VMAR_FLAG_SPECIFIC_OVERWRITE);
    bool is_specific = static_cast<bool>(vmar_flags & VMAR_FLAG_SPECIFIC) || is_specific_overwrite;
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

    vaddr_t new_base = -1;
    if (is_specific) {
        new_base = base_ + offset;
        if (!IS_PAGE_ALIGNED(new_base)) {
            return ERR_INVALID_ARGS;
        }
        if (align_pow2 > 0 && (new_base & ((1ULL << align_pow2) - 1))) {
            return ERR_INVALID_ARGS;
        }
        if (!IsRangeAvailableLocked(new_base, size)) {
            if (is_specific_overwrite) {
                return VmAddressRegion::OverwriteVmMapping(new_base, size, vmar_flags,
                                                           vmo, vmo_offset, arch_mmu_flags,
                                                           name, out);
            }
            return ERR_NO_MEMORY;
        }
    } else {
        // If we're not mapping to a specific place, search for an opening.
        new_base = AllocSpotLocked(size, align_pow2, arch_mmu_flags);
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
    if (vmar_flags & ~(VMAR_FLAG_SPECIFIC | VMAR_FLAG_SPECIFIC_OVERWRITE |
                       VMAR_CAN_RWX_FLAGS)) {
        return ERR_INVALID_ARGS;
    }

    // Validate that arch_mmu_flags does not contain any prohibited flags
    if (!is_valid_mapping_flags(arch_mmu_flags)) {
        return ERR_ACCESS_DENIED;
    }

    size = ROUNDUP(size, PAGE_SIZE);

    // Make sure that vmo_offset is aligned and that a mapping of this size
    // wouldn't overflow the vmo offset.
    if (!IS_PAGE_ALIGNED(vmo_offset) || vmo_offset + size < vmo_offset) {
        return ERR_INVALID_ARGS;
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

status_t VmAddressRegion::OverwriteVmMapping(vaddr_t base, size_t size, uint32_t vmar_flags,
                            mxtl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
                            uint arch_mmu_flags, const char* name,
                            mxtl::RefPtr<VmAddressRegionOrMapping>* out) {
    DEBUG_ASSERT(magic_ == kMagic);
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
    DEBUG_ASSERT(vmo);
    DEBUG_ASSERT(vmar_flags & VMAR_FLAG_SPECIFIC_OVERWRITE);

    AllocChecker ac;
    mxtl::RefPtr<VmAddressRegionOrMapping> vmar;
    vmar = mxtl::AdoptRef(new (&ac)
                          VmMapping(*this, base, size, vmar_flags,
                                    mxtl::move(vmo), vmo_offset, arch_mmu_flags, name));
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }

    status_t status = UnmapInternalLocked(base, size, false /* can_destroy_regions */);
    if (status != NO_ERROR) {
        return status;
    }

    vmar->Activate();
    *out = mxtl::move(vmar);
    return NO_ERROR;
}

status_t VmAddressRegion::DestroyLocked() {
    DEBUG_ASSERT(magic_ == kMagic);
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
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
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

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
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

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
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
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
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

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

vaddr_t VmAddressRegion::AllocSpotLocked(size_t size, uint8_t align_pow2, uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == kMagic);
    DEBUG_ASSERT(size > 0 && IS_PAGE_ALIGNED(size));
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    LTRACEF_LEVEL(2, "aspace %p size 0x%zx align %hhu\n", this, size,
                  align_pow2);

    if (aspace_->is_aslr_enabled()) {
        return NonCompactRandomizedRegionAllocatorLocked(size, align_pow2, arch_mmu_flags);
    }
    return LinearRegionAllocatorLocked(size, align_pow2, arch_mmu_flags);
}

bool VmAddressRegion::EnumerateChildrenLocked(VmEnumerator* ve, uint depth) {
    DEBUG_ASSERT(magic_ == kMagic);
    DEBUG_ASSERT(ve != nullptr);
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
    for (auto& child : subregions_) {
        DEBUG_ASSERT(child.IsAliveLocked());
        if (child.is_mapping()) {
            VmMapping* mapping = child.as_vm_mapping().get();
            DEBUG_ASSERT(mapping != nullptr);
            if (!ve->OnVmMapping(mapping, this, depth)) {
                return false;
            }
        } else {
            VmAddressRegion* vmar = child.as_vm_address_region().get();
            DEBUG_ASSERT(vmar != nullptr);
            if (!ve->OnVmAddressRegion(vmar, depth)) {
                return false;
            }
            if (!vmar->EnumerateChildrenLocked(ve, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

void VmAddressRegion::Dump(uint depth, bool verbose) const {
    DEBUG_ASSERT(magic_ == kMagic);
    for (uint i = 0; i < depth; ++i) {
        printf("  ");
    }
    printf("vmar %p [%#" PRIxPTR " %#" PRIxPTR "] sz %#zx ref %d '%s'\n", this,
           base_, base_ + size_ - 1, size_, ref_count_debug(), name_);
    for (const auto& child : subregions_) {
        child.Dump(depth + 1, verbose);
    }
}

void VmAddressRegion::Activate() {
    DEBUG_ASSERT(state_ == LifeCycleState::NOT_READY);
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    state_ = LifeCycleState::ALIVE;
    parent_->subregions_.insert(mxtl::RefPtr<VmAddressRegionOrMapping>(this));
}

status_t VmAddressRegion::Unmap(vaddr_t base, size_t size) {
    DEBUG_ASSERT(magic_ == kMagic);

    if (size == 0 || !IS_PAGE_ALIGNED(base)) {
        return ERR_INVALID_ARGS;
    }

    size = ROUNDUP(size, PAGE_SIZE);

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ERR_BAD_STATE;
    }

    return UnmapInternalLocked(base, size, true /* can_destroy_regions */);
}

status_t VmAddressRegion::UnmapInternalLocked(vaddr_t base, size_t size, bool can_destroy_regions) {
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    if (!is_in_range(base, size)) {
        return ERR_INVALID_ARGS;
    }

    if (subregions_.is_empty()) {
        return NO_ERROR;
    }

    const vaddr_t end_addr = base + size;
    const auto end = subregions_.lower_bound(end_addr);

    // Find the first region with a base greater than *base*.  If a region
    // exists for *base*, it will be immediately before it.
    auto begin = --subregions_.upper_bound(base);
    if (!begin.IsValid()) {
        begin = subregions_.begin();
    }

    // Check if we're partially spanning a subregion, or aren't allowed to
    // destroy regions and are spanning a region, and bail if we are
    for (auto itr = begin; itr != end; ++itr) {
        const vaddr_t itr_end = itr->base() + itr->size();
        if (!itr->is_mapping() && (!can_destroy_regions ||
                                   itr->base() < base || itr_end > end_addr)) {
            return ERR_INVALID_ARGS;
        }
    }

    for (auto itr = begin; itr != end;) {
        // Create a copy of the iterator, in case we destroy this element
        auto curr = itr++;

        const vaddr_t curr_end = curr->base() + curr->size();
        if (curr->is_mapping()) {
            const vaddr_t unmap_base = mxtl::max(curr->base(), base);
            const vaddr_t unmap_end = mxtl::min(curr_end, end_addr);
            const size_t unmap_size = unmap_end - unmap_base;

            // If we're unmapping the entire region, just call Destroy
            if (unmap_base == curr->base() && unmap_size == curr->size()) {
                __UNUSED status_t status = curr->as_vm_mapping()->DestroyLocked();
                DEBUG_ASSERT(status == NO_ERROR);
                continue;
            }

            // VmMapping::Unmap should only fail if it needs to allocate, which only
            // happens if it is unmapping from the middle of a region.  That can only
            // happen if there is only one region being operated on here, so we
            // can just forward along the error without having to rollback.
            // TODO(teisenbe): Technically arch_mmu_unmap() itself can also
            // fail.  We need to rework the system so that is no longer
            // possible.
            status_t status = curr->as_vm_mapping()->UnmapLocked(unmap_base, unmap_size);
            DEBUG_ASSERT(status == NO_ERROR || curr == begin);
            if (status != NO_ERROR) {
                return status;
            }
        } else {
            DEBUG_ASSERT(curr->base() >= base && curr_end <= end_addr);
            __UNUSED status_t status = curr->DestroyLocked();
            DEBUG_ASSERT(status == NO_ERROR);
        }
    }

    return NO_ERROR;
}

status_t VmAddressRegion::Protect(vaddr_t base, size_t size, uint new_arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == kMagic);

    if (size == 0 || !IS_PAGE_ALIGNED(base)) {
        return ERR_INVALID_ARGS;
    }

    size = ROUNDUP(size, PAGE_SIZE);

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ERR_BAD_STATE;
    }

    if (!is_in_range(base, size)) {
        return ERR_INVALID_ARGS;
    }

    if (subregions_.is_empty()) {
        return ERR_NOT_FOUND;
    }

    const vaddr_t end_addr = base + size;
    const auto end = subregions_.lower_bound(end_addr);

    // Find the first region with a base greater than *base*.  If a region
    // exists for *base*, it will be immediately before it.  If *base* isn't in
    // that entry, bail since it's unmapped.
    auto begin = --subregions_.upper_bound(base);
    if (!begin.IsValid() || begin->base() + begin->size() <= base) {
        return ERR_NOT_FOUND;
    }

    // Check if we're overlapping a subregion, or a part of the range is not
    // mapped, or the new permissions are invalid for some mapping in the range.
    vaddr_t last_mapped = begin->base();
    for (auto itr = begin; itr != end; ++itr) {
        if (!itr->is_mapping()) {
            return ERR_INVALID_ARGS;
        }
        if (itr->base() != last_mapped) {
            return ERR_NOT_FOUND;
        }
        if (!itr->is_valid_mapping_flags(new_arch_mmu_flags)) {
            return ERR_ACCESS_DENIED;
        }

        last_mapped = itr->base() + itr->size();
    }
    if (last_mapped < base + size) {
        return ERR_NOT_FOUND;
    }

    for (auto itr = begin; itr != end;) {
        DEBUG_ASSERT(itr->is_mapping());

        auto next = itr;
        ++next;

        const vaddr_t curr_end = itr->base() + itr->size();
        const vaddr_t protect_base = mxtl::max(itr->base(), base);
        const vaddr_t protect_end = mxtl::min(curr_end, end_addr);
        const size_t protect_size = protect_end - protect_base;

        status_t status = itr->as_vm_mapping()->ProtectLocked(protect_base, protect_size,
                                                              new_arch_mmu_flags);
        if (status != NO_ERROR) {
            // TODO(teisenbe): Try to work out a way to guarantee success, or
            // provide a full unwind?
            return status;
        }

        itr = mxtl::move(next);
    }

    return NO_ERROR;
}

vaddr_t VmAddressRegion::LinearRegionAllocatorLocked(size_t size, uint8_t align_pow2,
                                                     uint arch_mmu_flags) {
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    const vaddr_t base = 0;

    if (align_pow2 < PAGE_SIZE_SHIFT)
        align_pow2 = PAGE_SIZE_SHIFT;
    const vaddr_t align = 1UL << align_pow2;

    // Find the first gap in the address space which can contain a region of the
    // requested size.
    auto before_iter = subregions_.end();
    auto after_iter = subregions_.begin();

    do {
        vaddr_t spot;
        if (CheckGapLocked(before_iter, after_iter, &spot, base, align, size, 0, arch_mmu_flags)) {
            return spot;
        }

        before_iter = after_iter++;
    } while (before_iter.IsValid());

    // couldn't find anything
    return -1;
}

namespace {
    // Return the width (in bits) of the offset selection range available for ASLR
    uint NonCompactRandomizedAllocatorSelectWidth(bool is_user, size_t aspace_size,
                                                  size_t alloc_size) {
        // For userspace allocations, we will do a super-rough ASLR.  All
        // allocations will have (N - align_pow2) bits of entropy, where
        // N = floor(log2(aspace_size - allocation_size).)
        size_t max_offset = aspace_size - alloc_size;
        if (max_offset == 0) {
            return 0;
        }

        // Calculate floor(log2(max_offset)) to decide the range of values to
        // generate.  We remove all possible placement offsets greater than 1<<log2
        // for convenience to keep the distribution uniform.
        uint log2 = log2_ulong_floor(max_offset);

        if (is_user) {
            // BUG(MG-528): Cap log2 at 31 bits, since musl assumes 31-bit thread IDs and uses
            // thread struct addresses for those.
            log2 = mxtl::min(log2, 31u);
        }

        return log2;
    }
} // namespace

// Perform allocations for VMARs that aren't using the COMPACT policy.  This
// allocator works by choosing an offset in the VMAR in the
// range [0, size_ - size), and attempting to place the allocation there.  If
// the spot is occupied, the allocator retries several times before giving up.
vaddr_t VmAddressRegion::NonCompactRandomizedRegionAllocatorLocked(size_t size, uint8_t align_pow2,
                                                                   uint arch_mmu_flags) {
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    align_pow2 = mxtl::max(align_pow2, static_cast<uint8_t>(PAGE_SIZE_SHIFT));
    const vaddr_t align = 1UL << align_pow2;
    const uint aslr_width = NonCompactRandomizedAllocatorSelectWidth(aspace_->is_user(), size_,
                                                                     size);

    // If our width is 0, the only placement choice we have is the start of the
    // region (due to the allocation filling the region).
    if (aslr_width == 0) {
        vaddr_t spot;
        if (CheckGapLocked(subregions_.end(), subregions_.begin(), &spot, 0, align, size, 0,
                           arch_mmu_flags)) {
            return spot;
        }
        return -1;
    }

    // Generate a random offset from [0, 2^width). We choose a range
    // that is a power of 2 in size to make maintaining a uniform distribution
    // easier.
    const auto choose_offset = [this, align, aslr_width]() -> size_t {
        size_t chosen_offset;
        aspace_->AslrDraw(reinterpret_cast<uint8_t*>(&chosen_offset), sizeof(chosen_offset));
        chosen_offset &= ~(align - 1);
        chosen_offset &= (1UL << aslr_width) - 1;
        return chosen_offset;
    };

    // Generate a random placement, and see if we can fit there.  Return an
    // allocation failure if we can't find a placement after a number of tries.
    // TODO(teisenbe): Be more intelligent here.  Instead of searching uniformly
    // at random through the address space, look into generating uniformly at
    // random through the valid placements.  This may be tricky without turning
    // allocation into O(existing regions) or storing another tree of regions.
    for (size_t tries_remaining = 10; tries_remaining > 0; --tries_remaining) {
        const size_t offset = choose_offset();

        const vaddr_t chosen_base = base_ + offset;
        const vaddr_t chosen_end = chosen_base + size;
        DEBUG_ASSERT(chosen_end - 1 <= base_ + size_ - 1);

        // Attempt to use the generated address
        auto after_iter = subregions_.upper_bound(chosen_end - 1);
        auto before_iter = after_iter;

        if (after_iter == subregions_.begin() || subregions_.size() == 0) {
            before_iter = subregions_.end();
        } else {
            --before_iter;
        }

        if (before_iter != subregions_.end() && !before_iter.IsValid()) {
            continue;
        }

        vaddr_t spot;
        if (CheckGapLocked(before_iter, after_iter, &spot, chosen_base, align, size, 0,
                           arch_mmu_flags) && spot != static_cast<vaddr_t>(-1)) {
            return spot;
        }
    }

    // failed to find a placement
    return -1;
}
