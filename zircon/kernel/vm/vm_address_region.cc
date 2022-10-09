// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/vm_address_region.h"

#include <align.h>
#include <assert.h>
#include <inttypes.h>
#include <lib/crypto/prng.h>
#include <lib/userabi/vdso.h>
#include <pow2.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <ktl/algorithm.h>
#include <ktl/limits.h>
#include <vm/fault.h>
#include <vm/vm.h>
#include <vm/vm_address_region_enumerator.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

#include "vm_priv.h"

#include <ktl/enforce.h>

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

VmAddressRegion::VmAddressRegion(VmAspace& aspace, vaddr_t base, size_t size, uint32_t vmar_flags)
    : VmAddressRegionOrMapping(base, size, vmar_flags | VMAR_CAN_RWX_FLAGS, &aspace, nullptr,
                               false) {
  // We add in CAN_RWX_FLAGS above, since an address space can't usefully
  // contain a process without all of these.

  strlcpy(const_cast<char*>(name_), "root", sizeof(name_));
  LTRACEF("%p '%s'\n", this, name_);
}

VmAddressRegion::VmAddressRegion(VmAddressRegion& parent, vaddr_t base, size_t size,
                                 uint32_t vmar_flags, const char* name)
    : VmAddressRegionOrMapping(base, size, vmar_flags, parent.aspace_.get(), &parent, false) {
  strlcpy(const_cast<char*>(name_), name, sizeof(name_));
  LTRACEF("%p '%s'\n", this, name_);
}

VmAddressRegion::VmAddressRegion(VmAspace& kernel_aspace)
    : VmAddressRegion(kernel_aspace, kernel_aspace.base(), kernel_aspace.size(),
                      VMAR_FLAG_CAN_MAP_SPECIFIC) {
  // Activate the kernel root aspace immediately
  state_ = LifeCycleState::ALIVE;
}

zx_status_t VmAddressRegion::CreateRootLocked(VmAspace& aspace, uint32_t vmar_flags,
                                              fbl::RefPtr<VmAddressRegion>* out) {
  DEBUG_ASSERT(out);

  fbl::AllocChecker ac;
  auto vmar = new (&ac) VmAddressRegion(aspace, aspace.base(), aspace.size(), vmar_flags);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  AssertHeld(vmar->lock_ref());

  vmar->state_ = LifeCycleState::ALIVE;
  *out = fbl::AdoptRef(vmar);
  return ZX_OK;
}

zx_status_t VmAddressRegion::CreateSubVmarInternal(size_t offset, size_t size, uint8_t align_pow2,
                                                   uint32_t vmar_flags, fbl::RefPtr<VmObject> vmo,
                                                   uint64_t vmo_offset, uint arch_mmu_flags,
                                                   const char* name,
                                                   fbl::RefPtr<VmAddressRegionOrMapping>* out) {
  DEBUG_ASSERT(out);

  Guard<CriticalMutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  if (size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Check if there are any RWX privileges that the child would have that the
  // parent does not.
  if (vmar_flags & ~flags_ & VMAR_CAN_RWX_FLAGS) {
    return ZX_ERR_ACCESS_DENIED;
  }

  const bool is_specific_overwrite = vmar_flags & VMAR_FLAG_SPECIFIC_OVERWRITE;
  const bool is_specific = (vmar_flags & VMAR_FLAG_SPECIFIC) || is_specific_overwrite;
  const bool is_upper_bound = vmar_flags & VMAR_FLAG_OFFSET_IS_UPPER_LIMIT;
  if (is_specific && is_upper_bound) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!is_specific && !is_upper_bound && offset != 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!IS_PAGE_ALIGNED(offset)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Check to see if a cache policy exists if a VMO is passed in. VMOs that do not support
  // cache policy return ERR_UNSUPPORTED, anything aside from that and ZX_OK is an error.
  if (vmo) {
    uint32_t cache_policy = vmo->GetMappingCachePolicy();
    // Warn in the event that we somehow receive a VMO that has a cache
    // policy set while also holding cache policy flags within the arch
    // flags. The only path that should be able to achieve this is if
    // something in the kernel maps into their aspace incorrectly.
    if ((arch_mmu_flags & ARCH_MMU_FLAG_CACHE_MASK) != 0 &&
        (arch_mmu_flags & ARCH_MMU_FLAG_CACHE_MASK) != cache_policy) {
      TRACEF(
          "warning: mapping %s has conflicting cache policies: vmo %02x "
          "arch_mmu_flags %02x.\n",
          name, cache_policy, arch_mmu_flags & ARCH_MMU_FLAG_CACHE_MASK);
    }
    arch_mmu_flags |= cache_policy;
  }

  // Check that we have the required privileges if we want a SPECIFIC or
  // UPPER_LIMIT mapping.
  if ((is_specific || is_upper_bound) && !(flags_ & VMAR_FLAG_CAN_MAP_SPECIFIC)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  if (!is_upper_bound && (offset >= size_ || size > size_ - offset)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (is_upper_bound && (offset > size_ || size > size_ || size > offset)) {
    return ZX_ERR_INVALID_ARGS;
  }

  vaddr_t new_base = ktl::numeric_limits<vaddr_t>::max();
  if (is_specific) {
    // This would not overflow because offset <= size_ - 1, base_ + offset <= base_ + size_ - 1.
    new_base = base_ + offset;
    if (align_pow2 > 0 && (new_base & ((1ULL << align_pow2) - 1))) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (!subregions_.IsRangeAvailable(new_base, size)) {
      if (is_specific_overwrite) {
        return OverwriteVmMappingLocked(new_base, size, vmar_flags, vmo, vmo_offset, arch_mmu_flags,
                                        out);
      }
      return ZX_ERR_ALREADY_EXISTS;
    }
  } else {
    // If we're not mapping to a specific place, search for an opening.
    const vaddr_t upper_bound =
        is_upper_bound ? base_ + offset : ktl::numeric_limits<vaddr_t>::max();
    zx_status_t status = AllocSpotLocked(size, align_pow2, arch_mmu_flags, &new_base, upper_bound);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Notice if this is an executable mapping from the vDSO VMO
  // before we lose the VMO reference via ktl::move(vmo).
  const bool is_vdso_code =
      (vmo && (arch_mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) && VDso::vmo_is_vdso(vmo));

  fbl::AllocChecker ac;
  fbl::RefPtr<VmAddressRegionOrMapping> vmar;
  if (vmo) {
    // Check that VMOs that back kernel mappings start of with their pages pinned, unless the
    // dynamic flag has been set to opt out of this specific check.
    DEBUG_ASSERT(aspace_->is_user() || aspace_->is_guest_physical() ||
                 vmar_flags & VMAR_FLAG_DEBUG_DYNAMIC_KERNEL_MAPPING ||
                 vmo->DebugIsRangePinned(vmo_offset, size));
    vmar = fbl::AdoptRef(new (&ac) VmMapping(*this, new_base, size, vmar_flags, ktl::move(vmo),
                                             is_upper_bound ? 0 : vmo_offset, arch_mmu_flags,
                                             VmMapping::Mergeable::NO));
  } else {
    vmar = fbl::AdoptRef(new (&ac) VmAddressRegion(*this, new_base, size, vmar_flags, name));
  }

  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if (is_vdso_code) {
    // For an executable mapping of the vDSO, allow only one per process
    // and only for the valid range of the image.
    if (aspace_->vdso_code_mapping_ || !VDso::valid_code_mapping(vmo_offset, size)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    aspace_->vdso_code_mapping_ = fbl::RefPtr<VmMapping>::Downcast(vmar);
  }

  AssertHeld(vmar->lock_ref());
  vmar->Activate();
  *out = ktl::move(vmar);

  return ZX_OK;
}

zx_status_t VmAddressRegion::CreateSubVmar(size_t offset, size_t size, uint8_t align_pow2,
                                           uint32_t vmar_flags, const char* name,
                                           fbl::RefPtr<VmAddressRegion>* out) {
  DEBUG_ASSERT(out);

  if (!IS_PAGE_ALIGNED(size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Check that only allowed flags have been set
  if (vmar_flags & ~(VMAR_FLAG_SPECIFIC | VMAR_FLAG_CAN_MAP_SPECIFIC | VMAR_FLAG_COMPACT |
                     VMAR_CAN_RWX_FLAGS | VMAR_FLAG_OFFSET_IS_UPPER_LIMIT)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<VmAddressRegionOrMapping> res;
  zx_status_t status = CreateSubVmarInternal(offset, size, align_pow2, vmar_flags, nullptr, 0,
                                             ARCH_MMU_FLAG_INVALID, name, &res);
  if (status != ZX_OK) {
    return status;
  }
  // TODO(teisenbe): optimize this
  *out = res->as_vm_address_region();
  return ZX_OK;
}

zx_status_t VmAddressRegion::CreateVmMapping(size_t mapping_offset, size_t size, uint8_t align_pow2,
                                             uint32_t vmar_flags, fbl::RefPtr<VmObject> vmo,
                                             uint64_t vmo_offset, uint arch_mmu_flags,
                                             const char* name, fbl::RefPtr<VmMapping>* out) {
  DEBUG_ASSERT(out);
  LTRACEF("%p %#zx %#zx %x\n", this, mapping_offset, size, vmar_flags);

  // Check that only allowed flags have been set
  if (vmar_flags & ~(VMAR_FLAG_SPECIFIC | VMAR_FLAG_SPECIFIC_OVERWRITE | VMAR_CAN_RWX_FLAGS |
                     VMAR_FLAG_OFFSET_IS_UPPER_LIMIT | VMAR_FLAG_DEBUG_DYNAMIC_KERNEL_MAPPING)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate that arch_mmu_flags does not contain any prohibited flags
  if (!is_valid_mapping_flags(arch_mmu_flags)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  if (!IS_PAGE_ALIGNED(vmo_offset)) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t mapping_size = ROUNDUP_PAGE_SIZE(size);
  // Make sure that rounding up the page size did not overflow.
  if (mapping_size < size) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  // Make sure that a mapping of this size wouldn't overflow the vmo offset.
  if (vmo_offset + mapping_size < vmo_offset) {
    return ZX_ERR_OUT_OF_RANGE;
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

  fbl::RefPtr<VmAddressRegionOrMapping> res;
  zx_status_t status = CreateSubVmarInternal(mapping_offset, mapping_size, align_pow2, vmar_flags,
                                             vmo, vmo_offset, arch_mmu_flags, name, &res);
  if (status != ZX_OK) {
    return status;
  }
  // TODO(fxb/101641): For the moment we forward the latency sensitivity permanently onto any VMO
  // that gets mapped.
  if (aspace_->IsLatencySensitive()) {
    vmo->MarkAsLatencySensitive();
  }
  // TODO(teisenbe): optimize this
  *out = res->as_vm_mapping();
  return ZX_OK;
}

zx_status_t VmAddressRegion::OverwriteVmMappingLocked(vaddr_t base, size_t size,
                                                      uint32_t vmar_flags,
                                                      fbl::RefPtr<VmObject> vmo,
                                                      uint64_t vmo_offset, uint arch_mmu_flags,
                                                      fbl::RefPtr<VmAddressRegionOrMapping>* out) {
  canary_.Assert();
  DEBUG_ASSERT(vmo);
  DEBUG_ASSERT(vmar_flags & VMAR_FLAG_SPECIFIC_OVERWRITE);

  fbl::AllocChecker ac;
  fbl::RefPtr<VmAddressRegionOrMapping> vmar;
  vmar = fbl::AdoptRef(new (&ac) VmMapping(*this, base, size, vmar_flags, ktl::move(vmo),
                                           vmo_offset, arch_mmu_flags, VmMapping::Mergeable::NO));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = UnmapInternalLocked(base, size, false /* can_destroy_regions */,
                                           false /* allow_partial_vmar */);
  if (status != ZX_OK) {
    return status;
  }

  AssertHeld(vmar->lock_ref());
  vmar->Activate();
  *out = ktl::move(vmar);
  return ZX_OK;
}

zx_status_t VmAddressRegion::DestroyLocked() {
  canary_.Assert();
  LTRACEF("%p '%s'\n", this, name_);

  // The cur reference prevents regions from being destructed after dropping
  // the last reference to them when removing from their parent.
  fbl::RefPtr<VmAddressRegion> cur(this);
  AssertHeld(cur->lock_ref());
  while (cur) {
    // Iterate through children destroying mappings. If we find a
    // subregion, stop so we can traverse down.
    fbl::RefPtr<VmAddressRegion> child_region = nullptr;
    while (!cur->subregions_.IsEmpty() && !child_region) {
      VmAddressRegionOrMapping* child = &cur->subregions_.front();
      if (child->is_mapping()) {
        AssertHeld(child->lock_ref());
        // DestroyLocked should remove this child from our list on success.
        zx_status_t status = child->DestroyLocked();
        if (status != ZX_OK) {
          // TODO(teisenbe): Do we want to handle this case differently?
          return status;
        }
      } else {
        child_region = child->as_vm_address_region();
      }
    }

    if (child_region) {
      // If we found a child region, traverse down the tree.
      cur = child_region;
    } else {
      // All children are destroyed, so now destroy the current node.
      if (cur->parent_) {
        DEBUG_ASSERT(cur->in_subregion_tree());
        AssertHeld(cur->parent_->lock_ref());
        cur->parent_->subregions_.RemoveRegion(cur.get());
      }
      cur->state_ = LifeCycleState::DEAD;
      VmAddressRegion* cur_parent = cur->parent_;
      cur->parent_ = nullptr;

      // If we destroyed the original node, stop. Otherwise traverse
      // up the tree and keep destroying.
      cur.reset((cur.get() == this) ? nullptr : cur_parent);
    }
  }
  return ZX_OK;
}

fbl::RefPtr<VmAddressRegionOrMapping> VmAddressRegion::FindRegion(vaddr_t addr) {
  Guard<CriticalMutex> guard{aspace_->lock()};
  return FindRegionLocked(addr);
}

fbl::RefPtr<VmAddressRegionOrMapping> VmAddressRegion::FindRegionLocked(vaddr_t addr) {
  if (state_ != LifeCycleState::ALIVE) {
    return nullptr;
  }
  return fbl::RefPtr(subregions_.FindRegion(addr));
}

VmObject::AttributionCounts VmAddressRegion::AllocatedPagesLocked() const {
  canary_.Assert();

  AttributionCounts page_counts;
  if (state_ != LifeCycleState::ALIVE) {
    return page_counts;
  }

  for (const auto& child : subregions_) {
    AssertHeld(child.lock_ref());
    page_counts += child.AllocatedPagesLocked();
  }
  return page_counts;
}

zx_status_t VmAddressRegion::PageFault(vaddr_t va, uint pf_flags, LazyPageRequest* page_request) {
  canary_.Assert();

  VmAddressRegion* vmar = this;
  AssertHeld(vmar->lock_ref());
  while (VmAddressRegionOrMapping* next = vmar->subregions_.FindRegion(va)) {
    if (auto mapping = next->as_vm_mapping_ptr()) {
      AssertHeld(mapping->lock_ref());
      // Stash the mapping we found as the most recent fault. As we just found this mapping in the
      // VMAR tree we know it's in the ALIVE state, satisfying that requirement that allows us to
      // record this as a raw pointer.
      aspace_->last_fault_ = mapping;
      return mapping->PageFault(va, pf_flags, page_request);
    }
    vmar = next->as_vm_address_region_ptr();
  }

  return ZX_ERR_NOT_FOUND;
}

ktl::optional<vaddr_t> VmAddressRegion::CheckGapLocked(VmAddressRegionOrMapping* prev,
                                                       VmAddressRegionOrMapping* next,
                                                       vaddr_t search_base, vaddr_t align,
                                                       size_t region_size, size_t min_gap,
                                                       uint arch_mmu_flags) {
  vaddr_t gap_beg;  // first byte of a gap
  vaddr_t gap_end;  // last byte of a gap

  // compute the starting address of the gap
  if (prev != nullptr) {
    if (add_overflow(prev->base(), prev->size(), &gap_beg) ||
        add_overflow(gap_beg, min_gap, &gap_beg)) {
      return ktl::nullopt;
    }
  } else {
    gap_beg = base_;
  }

  // compute the ending address of the gap
  if (next != nullptr) {
    if (gap_beg == next->base()) {
      return ktl::nullopt;  // no gap between regions
    }
    if (sub_overflow(next->base(), 1, &gap_end) || sub_overflow(gap_end, min_gap, &gap_end)) {
      return ktl::nullopt;
    }
  } else {
    if (gap_beg - base_ == size_) {
      return ktl::nullopt;  // no gap at the end of address space.
    }
    if (add_overflow(base_, size_ - 1, &gap_end)) {
      return ktl::nullopt;
    }
  }

  DEBUG_ASSERT(gap_end > gap_beg);

  // trim it to the search range
  if (gap_end <= search_base) {
    return ktl::nullopt;
  }
  if (gap_beg < search_base) {
    gap_beg = search_base;
  }

  DEBUG_ASSERT(gap_end > gap_beg);

  LTRACEF_LEVEL(2, "search base %#" PRIxPTR " gap_beg %#" PRIxPTR " end %#" PRIxPTR "\n",
                search_base, gap_beg, gap_end);

  vaddr_t va =
      aspace_->arch_aspace().PickSpot(gap_beg, gap_end, align, region_size, arch_mmu_flags);

  if (va < gap_beg) {
    return ktl::nullopt;  // address wrapped around
  }

  if (va >= gap_end || ((gap_end - va + 1) < region_size)) {
    return ktl::nullopt;  // not enough room
  }

  return va;
}

template <typename ON_VMAR, typename ON_MAPPING>
zx_status_t VmAddressRegion::EnumerateChildrenInternalLocked(vaddr_t min_addr, vaddr_t max_addr,
                                                             ON_VMAR on_vmar,
                                                             ON_MAPPING on_mapping) {
  canary_.Assert();

  VmAddressRegionEnumerator<VmAddressRegionEnumeratorType::UnpausableVmarOrMapping> enumerator(
      *this, min_addr, max_addr);
  AssertHeld(enumerator.lock_ref());
  while (auto result = enumerator.next()) {
    // Lock is held over the entire duration so we can treat this as a raw pointer, knowing it will
    // not go away.
    VmAddressRegionOrMapping* curr = result->region_or_mapping;
    if (curr->is_mapping()) {
      VmMapping* mapping = curr->as_vm_mapping().get();

      DEBUG_ASSERT(mapping != nullptr);
      AssertHeld(mapping->lock_ref());
      if (!on_mapping(mapping, this, result->depth)) {
        return ZX_ERR_CANCELED;
      }
    } else {
      VmAddressRegion* vmar = curr->as_vm_address_region().get();
      DEBUG_ASSERT(vmar != nullptr);
      AssertHeld(vmar->lock_ref());
      if (!on_vmar(vmar, result->depth)) {
        return ZX_ERR_CANCELED;
      }
    }
  }
  return ZX_OK;
}

zx_status_t VmAddressRegion::EnumerateChildrenLocked(VmEnumerator* ve) {
  canary_.Assert();
  DEBUG_ASSERT(ve != nullptr);

  return EnumerateChildrenInternalLocked(
      0, UINT64_MAX,
      [ve](const VmAddressRegion* vmar, uint depth) {
        AssertHeld(vmar->lock_ref());
        return ve->OnVmAddressRegion(vmar, depth);
      },
      [ve](const VmMapping* map, const VmAddressRegion* vmar, uint depth) {
        AssertHeld(vmar->lock_ref());
        AssertHeld(map->lock_ref());
        return ve->OnVmMapping(map, vmar, depth);
      });
}

bool VmAddressRegion::has_parent() const {
  Guard<CriticalMutex> guard{aspace_->lock()};
  return parent_ != nullptr;
}

void VmAddressRegion::DumpLocked(uint depth, bool verbose) const {
  canary_.Assert();
  for (uint i = 0; i < depth; ++i) {
    printf("  ");
  }
  printf("vmar %p [%#" PRIxPTR " %#" PRIxPTR "] sz %#zx ref %d state %d '%s'\n", this, base_,
         base_ + (size_ - 1), size_, ref_count_debug(), (int)state_, name_);
  for (const auto& child : subregions_) {
    AssertHeld(child.lock_ref());
    child.DumpLocked(depth + 1, verbose);
  }
}

void VmAddressRegion::Activate() {
  DEBUG_ASSERT(state_ == LifeCycleState::NOT_READY);

  state_ = LifeCycleState::ALIVE;
  AssertHeld(parent_->lock_ref());

  // Validate we are a correct child of our parent.
  DEBUG_ASSERT(parent_->is_in_range(base_, size_));

  // Look for a region in the parent starting from our desired base. If any region is found, make
  // sure we do not intersect with it.
  auto candidate = parent_->subregions_.IncludeOrHigher(base_);
  ASSERT(candidate == parent_->subregions_.end() || candidate->base_ >= base_ + size_);

  parent_->subregions_.InsertRegion(fbl::RefPtr<VmAddressRegionOrMapping>(this));
}

zx_status_t VmAddressRegion::RangeOp(RangeOpType op, vaddr_t base, size_t len,
                                     user_inout_ptr<void> buffer, size_t buffer_size) {
  canary_.Assert();
  if (buffer || buffer_size) {
    return ZX_ERR_INVALID_ARGS;
  }
  len = ROUNDUP(len, PAGE_SIZE);
  if (len == 0 || !IS_PAGE_ALIGNED(base)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (!is_in_range(base, len)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const vaddr_t last_addr = base + len;

  if (op == RangeOpType::AlwaysNeed) {
    // TODO(fxb/101641): For the moment marking any part of the address space as always need causes
    // the entire aspace to be considered latency sensitive.
    aspace_->MarkAsLatencySensitive();
  }

  Guard<CriticalMutex> guard{aspace_->lock()};
  // Capture the validation that we need to do whenever the lock is acquired.
  auto validate = [this, base, len]() TA_REQ(aspace_->lock()) -> zx_status_t {
    if (state_ != LifeCycleState::ALIVE) {
      return ZX_ERR_BAD_STATE;
    }

    // Don't allow any operations on the vDSO code mapping.
    if (aspace_->IntersectsVdsoCodeLocked(base, len)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    return ZX_OK;
  };
  if (zx_status_t s = validate(); s != ZX_OK) {
    return s;
  }

  VmAddressRegionEnumerator<VmAddressRegionEnumeratorType::PausableMapping> enumerator(*this, base,
                                                                                       last_addr);
  AssertHeld(enumerator.lock_ref());
  vaddr_t expected = base;
  while (auto map = enumerator.next()) {
    // Presently we hold the lock, so we know that region_or_mapping is valid, but we want to use
    // this outside of the lock later on, and so we must upgrade it to a RefPtr.
    fbl::RefPtr<VmMapping> mapping = fbl::RefPtr<VmMapping>(map->region_or_mapping);
    AssertHeld(mapping->lock_ref());

    // It's possible base is less than expected if the first mapping is not precisely aligned
    // to the start of our range. After that base should always be expected, and if it's
    // greater then there is a gap and this is considered an error.
    if (mapping->base() > expected) {
      return ZX_ERR_BAD_STATE;
    }
    // We should only have been called if we were at least partially in range.
    DEBUG_ASSERT(mapping->is_in_range(expected, 1));
    const size_t mapping_offset = expected - mapping->base();
    const size_t vmo_offset = mapping->object_offset_locked() + mapping_offset;

    // Should only have been called for a non-zero range.
    DEBUG_ASSERT(last_addr > expected);

    const size_t total_remain = last_addr - expected;
    DEBUG_ASSERT(mapping->size() > mapping_offset);
    const size_t max_in_mapping = mapping->size() - mapping_offset;

    const size_t size = ktl::min(total_remain, max_in_mapping);

    fbl::RefPtr<VmObject> vmo = mapping->vmo_locked();

    zx_status_t result = ZX_OK;
    enumerator.pause();
    guard.CallUnlocked([&result, &vmo, &mapping, op, mapping_offset, vmo_offset, size] {
      switch (op) {
        case RangeOpType::Commit:
          if (!mapping->is_valid_mapping_flags(ARCH_MMU_FLAG_PERM_WRITE)) {
            result = ZX_ERR_ACCESS_DENIED;
          } else {
            result = vmo->CommitRange(vmo_offset, size);
            if (result == ZX_OK) {
              result = mapping->MapRange(mapping_offset, size, /*commit=*/false,
                                         /*ignore_existing=*/true);
            }
          }
          break;
        case RangeOpType::Decommit:
          // Decommit zeroes pages of the VMO, equivalent to writing to it.
          // the mapping is currently writable, or could be made writable.
          if (!mapping->is_valid_mapping_flags(ARCH_MMU_FLAG_PERM_WRITE)) {
            result = ZX_ERR_ACCESS_DENIED;
          } else {
            result = vmo->DecommitRange(vmo_offset, size);
          }
          break;
        case RangeOpType::MapRange:
          result = mapping->MapRange(mapping_offset, size, /*commit=*/false,
                                     /*ignore_existing=*/true);
          break;
        case RangeOpType::AlwaysNeed:
          result = vmo->HintRange(vmo_offset, size, VmObject::EvictionHint::AlwaysNeed);
          if (result == ZX_OK) {
            result = mapping->MapRange(mapping_offset, size, /*commit=*/false,
                                       /*ignore_existing=*/true);
          }
          break;
        case RangeOpType::DontNeed:
          result = vmo->HintRange(vmo_offset, size, VmObject::EvictionHint::DontNeed);
          break;
        default:
          result = ZX_ERR_NOT_SUPPORTED;
          break;
      }
    });
    // Since the lock was dropped we must re-validate before doing anything else.
    if (zx_status_t s = validate(); s != ZX_OK) {
      return s;
    }
    enumerator.resume();

    if (result != ZX_OK) {
      return result;
    }
    expected += size;
  }

  // Check if there was a gap right at the end of the range.
  if (expected < last_addr) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t VmAddressRegion::Unmap(vaddr_t base, size_t size) {
  canary_.Assert();

  size = ROUNDUP(size, PAGE_SIZE);
  if (size == 0 || !IS_PAGE_ALIGNED(base)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<CriticalMutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  return UnmapInternalLocked(base, size, true /* can_destroy_regions */,
                             false /* allow_partial_vmar */);
}

zx_status_t VmAddressRegion::UnmapAllowPartial(vaddr_t base, size_t size) {
  canary_.Assert();

  size = ROUNDUP(size, PAGE_SIZE);
  if (size == 0 || !IS_PAGE_ALIGNED(base)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<CriticalMutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  return UnmapInternalLocked(base, size, true /* can_destroy_regions */,
                             true /* allow_partial_vmar */);
}

zx_status_t VmAddressRegion::UnmapInternalLocked(vaddr_t base, size_t size,
                                                 bool can_destroy_regions,
                                                 bool allow_partial_vmar) {
  if (!is_in_range(base, size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (subregions_.IsEmpty()) {
    return ZX_OK;
  }

  // Any unmap spanning the vDSO code mapping is verboten.
  if (aspace_->IntersectsVdsoCodeLocked(base, size)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // The last byte of the current unmap range.
  vaddr_t end_addr_byte = 0;
  DEBUG_ASSERT(size > 0);
  bool overflowed = add_overflow(base, size - 1, &end_addr_byte);
  ASSERT(!overflowed);
  auto end = subregions_.UpperBound(end_addr_byte);
  auto begin = subregions_.IncludeOrHigher(base);

  if (!allow_partial_vmar) {
    // Check if we're partially spanning a subregion, or aren't allowed to
    // destroy regions and are spanning a region, and bail if we are.
    for (auto itr = begin; itr != end; ++itr) {
      vaddr_t itr_end_byte = 0;
      DEBUG_ASSERT(itr->size() > 0);
      overflowed = add_overflow(itr->base(), itr->size() - 1, &itr_end_byte);
      ASSERT(!overflowed);
      if (!itr->is_mapping() &&
          (!can_destroy_regions || itr->base() < base || itr_end_byte > end_addr_byte)) {
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }

  bool at_top = true;
  for (auto itr = begin; itr != end;) {
    uint64_t curr_base;
    VmAddressRegion* up;
    {
      // Create a copy of the iterator. It lives in this sub-scope as at the end we may have
      // destroyed. As such we stash a copy of its base in a variable in our outer scope.
      auto curr = itr++;
      AssertHeld(curr->lock_ref());
      curr_base = curr->base();
      // The parent will keep living even if we destroy curr so can place that in the outer scope.
      up = curr->parent_;

      if (curr->is_mapping()) {
        AssertHeld(curr->as_vm_mapping()->lock_ref());
        vaddr_t curr_end_byte = 0;
        DEBUG_ASSERT(curr->size() > 1);
        overflowed = add_overflow(curr->base(), curr->size() - 1, &curr_end_byte);
        ASSERT(!overflowed);
        const vaddr_t unmap_base = ktl::max(curr->base(), base);
        const vaddr_t unmap_end_byte = ktl::min(curr_end_byte, end_addr_byte);
        size_t unmap_size;
        overflowed = add_overflow(unmap_end_byte - unmap_base, 1, &unmap_size);
        ASSERT(!overflowed);

        if (unmap_base == curr->base() && unmap_size == curr->size()) {
          // If we're unmapping the entire region, just call Destroy
          __UNUSED zx_status_t status = curr->DestroyLocked();
          DEBUG_ASSERT(status == ZX_OK);
        } else {
          // VmMapping::Unmap should only fail if it needs to allocate,
          // which only happens if it is unmapping from the middle of a
          // region.  That can only happen if there is only one region
          // being operated on here, so we can just forward along the
          // error without having to rollback.
          //
          // TODO(teisenbe): Technically arch_mmu_unmap() itself can also
          // fail.  We need to rework the system so that is no longer
          // possible.
          zx_status_t status = curr->as_vm_mapping()->UnmapLocked(unmap_base, unmap_size);
          DEBUG_ASSERT(status == ZX_OK || curr == begin);
          if (status != ZX_OK) {
            return status;
          }
        }
      } else {
        vaddr_t unmap_base = 0;
        size_t unmap_size = 0;
        __UNUSED bool intersects =
            GetIntersect(base, size, curr->base(), curr->size(), &unmap_base, &unmap_size);
        DEBUG_ASSERT(intersects);
        if (allow_partial_vmar) {
          // If partial VMARs are allowed, we descend into sub-VMARs.
          fbl::RefPtr<VmAddressRegion> vmar = curr->as_vm_address_region();
          AssertHeld(vmar->lock_ref());
          if (!vmar->subregions_.IsEmpty()) {
            begin = vmar->subregions_.IncludeOrHigher(base);
            end = vmar->subregions_.UpperBound(end_addr_byte);
            itr = begin;
            at_top = false;
          }
        } else if (unmap_base == curr->base() && unmap_size == curr->size()) {
          __UNUSED zx_status_t status = curr->DestroyLocked();
          DEBUG_ASSERT(status == ZX_OK);
        }
      }
    }

    if (allow_partial_vmar && !at_top && itr == end) {
      AssertHeld(up->lock_ref());
      // If partial VMARs are allowed, and we have reached the end of a
      // sub-VMAR range, we ascend and continue iteration.
      do {
        // Use the stashed curr_base as if curr was a mapping we may have destroyed it.
        begin = up->subregions_.UpperBound(curr_base);
        if (begin.IsValid()) {
          break;
        }
        at_top = up == this;
        up = up->parent_;
      } while (!at_top);
      if (!begin.IsValid()) {
        // If we have reached the end after ascending all the way up,
        // break out of the loop.
        break;
      }
      end = up->subregions_.UpperBound(end_addr_byte);
      itr = begin;
    }
  }

  return ZX_OK;
}

zx_status_t VmAddressRegion::Protect(vaddr_t base, size_t size, uint new_arch_mmu_flags) {
  canary_.Assert();

  size = ROUNDUP(size, PAGE_SIZE);
  if (size == 0 || !IS_PAGE_ALIGNED(base)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<CriticalMutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  if (!is_in_range(base, size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (subregions_.IsEmpty()) {
    return ZX_ERR_NOT_FOUND;
  }

  // The last byte of the range.
  vaddr_t end_addr_byte = 0;
  bool overflowed = add_overflow(base, size - 1, &end_addr_byte);
  ASSERT(!overflowed);

  // Find the first region with a base greater than *base*.  If a region
  // exists for *base*, it will be immediately before it.  If *base* isn't in
  // that entry, bail since it's unmapped.
  auto begin = --subregions_.UpperBound(base);
  if (!begin.IsValid() || begin->size() <= base - begin->base()) {
    return ZX_ERR_NOT_FOUND;
  }

  // Check if we're overlapping a subregion, or a part of the range is not
  // mapped, or the new permissions are invalid for some mapping in the range.
  for (auto itr = begin;;) {
    VmMapping* mapping = itr->as_vm_mapping_ptr();
    if (!mapping) {
      return ZX_ERR_INVALID_ARGS;
    }

    if (!itr->is_valid_mapping_flags(new_arch_mmu_flags)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    if (mapping == aspace_->vdso_code_mapping_.get()) {
      return ZX_ERR_ACCESS_DENIED;
    }

    // The last byte of the last mapped region.
    vaddr_t last_mapped_byte = 0;
    overflowed = add_overflow(itr->base(), itr->size() - 1, &last_mapped_byte);
    ASSERT(!overflowed);
    if (last_mapped_byte >= end_addr_byte) {
      // This mapping either reaches exactly to, or beyond, the end of the range we are protecting,
      // so we are finished validating.
      break;
    }
    // As we still have some range to process we can require there to be another adjacent mapping,
    // so increment itr and check for it.

    ++itr;
    if (!itr.IsValid()) {
      return ZX_ERR_NOT_FOUND;
    }

    // As we are at least the second mapping in the address space, and mappings cannot be
    // zero sized, we should not have a base of 0.
    DEBUG_ASSERT(itr->base() > 0);
    if (itr->base() - 1 != last_mapped_byte) {
      return ZX_ERR_NOT_FOUND;
    }
  }

  for (auto itr = begin; itr.IsValid() && itr->base() < end_addr_byte;) {
    VmMapping* mapping = itr->as_vm_mapping_ptr();
    DEBUG_ASSERT(mapping);

    // The last byte of the current region.
    vaddr_t curr_end_byte = 0;
    overflowed = add_overflow(itr->base(), itr->size() - 1, &curr_end_byte);
    ASSERT(!overflowed);
    const vaddr_t protect_base = ktl::max(itr->base(), base);
    const vaddr_t protect_end_byte = ktl::min(curr_end_byte, end_addr_byte);
    size_t protect_size;
    overflowed = add_overflow(protect_end_byte - protect_base, 1, &protect_size);
    ASSERT(!overflowed);
    AssertHeld(mapping->lock_ref());

    // |itr| needs to be incremented here since the mapping might be deleted by ProtectLocked. After
    // |itr| is incremented we can use |mapping| instead, although after ProtectLocked is called it
    // also becomes invalid.
    itr++;
    zx_status_t status = mapping->ProtectLocked(protect_base, protect_size, new_arch_mmu_flags);
    if (status != ZX_OK) {
      // TODO(teisenbe): Try to work out a way to guarantee success, or
      // provide a full unwind?
      return status;
    }
  }

  return ZX_OK;
}

// Perform allocations for VMARs. This allocator works by choosing uniformly at random from a set of
// positions that could satisfy the allocation. The set of positions are the 'left' most positions
// of the address space and are capped by the address entropy limit. The entropy limit is retrieved
// from the address space, and can vary based on whether the user has requested compact allocations
// or not.
zx_status_t VmAddressRegion::AllocSpotLocked(size_t size, uint8_t align_pow2, uint arch_mmu_flags,
                                             vaddr_t* spot, vaddr_t upper_limit) {
  canary_.Assert();
  DEBUG_ASSERT(size > 0 && IS_PAGE_ALIGNED(size));
  DEBUG_ASSERT(spot);

  LTRACEF_LEVEL(2, "aspace %p size 0x%zx align %hhu upper_limit 0x%lx\n", this, size, align_pow2,
                upper_limit);

  align_pow2 = ktl::max(align_pow2, static_cast<uint8_t>(PAGE_SIZE_SHIFT));
  const vaddr_t align = 1UL << align_pow2;
  // Ensure our candidate calculation shift will not overflow.
  const uint8_t entropy = aspace_->AslrEntropyBits(flags_ & VMAR_FLAG_COMPACT);
  vaddr_t alloc_spot = 0;
  crypto::Prng* prng = nullptr;
  if (aspace_->is_aslr_enabled()) {
    prng = &aspace_->AslrPrngLocked();
  }

  zx_status_t status = subregions_.GetAllocSpot(&alloc_spot, align_pow2, entropy, size, base_,
                                                size_, prng, upper_limit);
  if (status != ZX_OK) {
    return status;
  }

  // Sanity check that the allocation fits.
  vaddr_t alloc_last_byte;
  bool overflowed = add_overflow(alloc_spot, size - 1, &alloc_last_byte);
  ASSERT(!overflowed);
  auto after_iter = subregions_.UpperBound(alloc_last_byte);
  auto before_iter = after_iter;

  if (after_iter == subregions_.begin() || subregions_.IsEmpty()) {
    before_iter = subregions_.end();
  } else {
    --before_iter;
  }

  ASSERT(before_iter == subregions_.end() || before_iter.IsValid());
  VmAddressRegionOrMapping* before = nullptr;
  if (before_iter.IsValid()) {
    before = &(*before_iter);
  }
  VmAddressRegionOrMapping* after = nullptr;
  if (after_iter.IsValid()) {
    after = &(*after_iter);
  }
  if (auto va = CheckGapLocked(before, after, alloc_spot, align, size, 0, arch_mmu_flags)) {
    *spot = *va;
    return ZX_OK;
  }
  panic("Unexpected allocation failure\n");
}

zx_status_t VmAddressRegion::ReserveSpace(const char* name, vaddr_t base, size_t size,
                                          uint arch_mmu_flags) {
  canary_.Assert();
  if (!is_in_range(base, size)) {
    return ZX_ERR_INVALID_ARGS;
  }
  size_t offset = base - base_;
  // We need a zero-length VMO to pass into CreateVmMapping so that a VmMapping would be created.
  // The VmMapping is already mapped to physical pages in start.S.
  // We would never call MapRange on the VmMapping, thus the VMO would never actually allocate any
  // physical pages and we would never modify the PTE except for the permission change bellow
  // caused by Protect.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, 0, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  vmo->set_name(name, strlen(name));
  // allocate a region and put it in the aspace list.
  // Need to set the VMAR_FLAG_DEBUG_DYNAMIC_KERNEL_MAPPING since we are 'cheating' with this fake
  // zero-length VMO and so the checks that the pages in that VMO are pinned would otherwise fail.
  fbl::RefPtr<VmMapping> r(nullptr);
  status =
      CreateVmMapping(offset, size, 0, VMAR_FLAG_SPECIFIC | VMAR_FLAG_DEBUG_DYNAMIC_KERNEL_MAPPING,
                      vmo, 0, arch_mmu_flags, name, &r);
  if (status != ZX_OK) {
    return status;
  }
  // Directly invoke a protect on the hardware aspace to modify the protection of the existing
  // mappings. If the desired protection flags is "no permissions" then we need to use unmap instead
  // of protect since a mapping with no permissions is not valid on most architectures.
  if ((arch_mmu_flags & ARCH_MMU_FLAG_PERM_RWX_MASK) == 0) {
    return aspace_->arch_aspace().Unmap(base, size / PAGE_SIZE, ArchVmAspace::EnlargeOperation::No,
                                        nullptr);
  } else {
    return aspace_->arch_aspace().Protect(base, size / PAGE_SIZE, arch_mmu_flags);
  }
}
