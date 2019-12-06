// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/vm_address_region.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <lib/userabi/vdso.h>
#include <pow2.h>
#include <trace.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

#include "vm_priv.h"

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

VmAddressRegion::VmAddressRegion(VmAspace& aspace, vaddr_t base, size_t size, uint32_t vmar_flags)
    : VmAddressRegionOrMapping(base, size, vmar_flags | VMAR_CAN_RWX_FLAGS, &aspace, nullptr) {
  // We add in CAN_RWX_FLAGS above, since an address space can't usefully
  // contain a process without all of these.

  strlcpy(const_cast<char*>(name_), "root", sizeof(name_));
  LTRACEF("%p '%s'\n", this, name_);
}

VmAddressRegion::VmAddressRegion(VmAddressRegion& parent, vaddr_t base, size_t size,
                                 uint32_t vmar_flags, const char* name)
    : VmAddressRegionOrMapping(base, size, vmar_flags, parent.aspace_.get(), &parent) {
  strlcpy(const_cast<char*>(name_), name, sizeof(name_));
  LTRACEF("%p '%s'\n", this, name_);
}

VmAddressRegion::VmAddressRegion(VmAspace& kernel_aspace)
    : VmAddressRegion(kernel_aspace, kernel_aspace.base(), kernel_aspace.size(),
                      VMAR_FLAG_CAN_MAP_SPECIFIC) {
  // Activate the kernel root aspace immediately
  state_ = LifeCycleState::ALIVE;
}

VmAddressRegion::VmAddressRegion() : VmAddressRegionOrMapping(0, 0, 0, nullptr, nullptr) {
  strlcpy(const_cast<char*>(name_), "dummy", sizeof(name_));
  LTRACEF("%p '%s'\n", this, name_);
}

zx_status_t VmAddressRegion::CreateRoot(VmAspace& aspace, uint32_t vmar_flags,
                                        fbl::RefPtr<VmAddressRegion>* out) {
  DEBUG_ASSERT(out);

  fbl::AllocChecker ac;
  auto vmar = new (&ac) VmAddressRegion(aspace, aspace.base(), aspace.size(), vmar_flags);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

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

  Guard<fbl::Mutex> guard{aspace_->lock()};
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

  bool is_specific_overwrite = static_cast<bool>(vmar_flags & VMAR_FLAG_SPECIFIC_OVERWRITE);
  bool is_specific = static_cast<bool>(vmar_flags & VMAR_FLAG_SPECIFIC) || is_specific_overwrite;
  if (!is_specific && offset != 0) {
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

  // Check that we have the required privileges if we want a SPECIFIC mapping
  if (is_specific && !(flags_ & VMAR_FLAG_CAN_MAP_SPECIFIC)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  if (offset >= size_ || size > size_ - offset) {
    return ZX_ERR_INVALID_ARGS;
  }

  vaddr_t new_base = -1;
  if (is_specific) {
    new_base = base_ + offset;
    if (!IS_PAGE_ALIGNED(new_base)) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (align_pow2 > 0 && (new_base & ((1ULL << align_pow2) - 1))) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (!IsRangeAvailableLocked(new_base, size)) {
      if (is_specific_overwrite) {
        return OverwriteVmMapping(new_base, size, vmar_flags, vmo, vmo_offset, arch_mmu_flags, out);
      }
      return ZX_ERR_NO_MEMORY;
    }
  } else {
    // If we're not mapping to a specific place, search for an opening.
    zx_status_t status = AllocSpotLocked(size, align_pow2, arch_mmu_flags, &new_base);
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
    vmar = fbl::AdoptRef(new (&ac) VmMapping(*this, new_base, size, vmar_flags, ktl::move(vmo),
                                             vmo_offset, arch_mmu_flags));
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
  if (vmar_flags &
      ~(VMAR_FLAG_SPECIFIC | VMAR_FLAG_CAN_MAP_SPECIFIC | VMAR_FLAG_COMPACT | VMAR_CAN_RWX_FLAGS)) {
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
  if (vmar_flags & ~(VMAR_FLAG_SPECIFIC | VMAR_FLAG_SPECIFIC_OVERWRITE | VMAR_CAN_RWX_FLAGS)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate that arch_mmu_flags does not contain any prohibited flags
  if (!is_valid_mapping_flags(arch_mmu_flags)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // If size overflows, it'll become 0 and get rejected in
  // CreateSubVmarInternal.
  size = ROUNDUP(size, PAGE_SIZE);

  // Make sure that vmo_offset is aligned and that a mapping of this size
  // wouldn't overflow the vmo offset.
  if (!IS_PAGE_ALIGNED(vmo_offset) || vmo_offset + size < vmo_offset) {
    return ZX_ERR_INVALID_ARGS;
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
  zx_status_t status =
      CreateSubVmarInternal(mapping_offset, size, align_pow2, vmar_flags, ktl::move(vmo),
                            vmo_offset, arch_mmu_flags, name, &res);
  if (status != ZX_OK) {
    return status;
  }
  // TODO(teisenbe): optimize this
  *out = res->as_vm_mapping();
  return ZX_OK;
}

zx_status_t VmAddressRegion::OverwriteVmMapping(vaddr_t base, size_t size, uint32_t vmar_flags,
                                                fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
                                                uint arch_mmu_flags,
                                                fbl::RefPtr<VmAddressRegionOrMapping>* out) {
  canary_.Assert();
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());
  DEBUG_ASSERT(vmo);
  DEBUG_ASSERT(vmar_flags & VMAR_FLAG_SPECIFIC_OVERWRITE);

  fbl::AllocChecker ac;
  fbl::RefPtr<VmAddressRegionOrMapping> vmar;
  vmar = fbl::AdoptRef(new (&ac) VmMapping(*this, base, size, vmar_flags, ktl::move(vmo),
                                           vmo_offset, arch_mmu_flags));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = UnmapInternalLocked(base, size, false /* can_destroy_regions */,
                                           false /* allow_partial_vmar */);
  if (status != ZX_OK) {
    return status;
  }

  vmar->Activate();
  *out = ktl::move(vmar);
  return ZX_OK;
}

zx_status_t VmAddressRegion::DestroyLocked() {
  canary_.Assert();
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());
  LTRACEF("%p '%s'\n", this, name_);

  // The cur reference prevents regions from being destructed after dropping
  // the last reference to them when removing from their parent.
  fbl::RefPtr<VmAddressRegion> cur(this);
  while (cur) {
    // Iterate through children destroying mappings. If we find a
    // subregion, stop so we can traverse down.
    fbl::RefPtr<VmAddressRegion> child_region = nullptr;
    while (!cur->subregions_.is_empty() && !child_region) {
      VmAddressRegionOrMapping* child = &cur->subregions_.front();
      if (child->is_mapping()) {
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
        DEBUG_ASSERT(cur->subregion_list_node_.InContainer());
        cur->parent_->RemoveSubregion(cur.get());
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

void VmAddressRegion::RemoveSubregion(VmAddressRegionOrMapping* region) {
  subregions_.erase(*region);
}

fbl::RefPtr<VmAddressRegionOrMapping> VmAddressRegion::FindRegion(vaddr_t addr) {
  Guard<fbl::Mutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return nullptr;
  }
  return FindRegionLocked(addr);
}

fbl::RefPtr<VmAddressRegionOrMapping> VmAddressRegion::FindRegionLocked(vaddr_t addr) {
  canary_.Assert();

  // Find the first region with a base greater than *addr*.  If a region
  // exists for *addr*, it will be immediately before it.
  auto itr = --subregions_.upper_bound(addr);
  if (!itr.IsValid() || itr->base() > addr || addr > itr->base() + itr->size() - 1) {
    return nullptr;
  }

  return itr.CopyPointer();
}

size_t VmAddressRegion::AllocatedPagesLocked() const {
  canary_.Assert();
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());

  if (state_ != LifeCycleState::ALIVE) {
    return 0;
  }

  size_t sum = 0;
  for (const auto& child : subregions_) {
    sum += child.AllocatedPagesLocked();
  }
  return sum;
}

zx_status_t VmAddressRegion::PageFault(vaddr_t va, uint pf_flags, PageRequest* page_request) {
  canary_.Assert();
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());

  auto vmar = fbl::RefPtr(this);
  while (auto next = vmar->FindRegionLocked(va)) {
    if (next->is_mapping()) {
      return next->PageFault(va, pf_flags, page_request);
    }
    vmar = next->as_vm_address_region();
  }

  return ZX_ERR_NOT_FOUND;
}

bool VmAddressRegion::IsRangeAvailableLocked(vaddr_t base, size_t size) {
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());
  DEBUG_ASSERT(size > 0);

  // Find the first region with base > *base*.  Since subregions_ has no
  // overlapping elements, we just need to check this one and the prior
  // child.

  auto prev = subregions_.upper_bound(base);
  auto next = prev--;

  if (prev.IsValid()) {
    vaddr_t prev_last_byte;
    if (add_overflow(prev->base(), prev->size() - 1, &prev_last_byte)) {
      return false;
    }
    if (prev_last_byte >= base) {
      return false;
    }
  }

  if (next.IsValid() && next != subregions_.end()) {
    vaddr_t last_byte;
    if (add_overflow(base, size - 1, &last_byte)) {
      return false;
    }
    if (next->base() <= last_byte) {
      return false;
    }
  }
  return true;
}

bool VmAddressRegion::CheckGapLocked(const ChildList::iterator& prev,
                                     const ChildList::iterator& next, vaddr_t* pva,
                                     vaddr_t search_base, vaddr_t align, size_t region_size,
                                     size_t min_gap, uint arch_mmu_flags) {
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());

  vaddr_t gap_beg;  // first byte of a gap
  vaddr_t gap_end;  // last byte of a gap

  uint prev_arch_mmu_flags;
  uint next_arch_mmu_flags;

  DEBUG_ASSERT(pva);

  // compute the starting address of the gap
  if (prev.IsValid()) {
    if (add_overflow(prev->base(), prev->size(), &gap_beg) ||
        add_overflow(gap_beg, min_gap, &gap_beg)) {
      goto not_found;
    }
  } else {
    gap_beg = base_;
  }

  // compute the ending address of the gap
  if (next.IsValid()) {
    if (gap_beg == next->base()) {
      goto next_gap;  // no gap between regions
    }
    if (sub_overflow(next->base(), 1, &gap_end) || sub_overflow(gap_end, min_gap, &gap_end)) {
      goto not_found;
    }
  } else {
    if (gap_beg == base_ + size_) {
      goto not_found;  // no gap at the end of address space. Stop search
    }
    if (add_overflow(base_, size_ - 1, &gap_end)) {
      goto not_found;
    }
  }

  DEBUG_ASSERT(gap_end > gap_beg);

  // trim it to the search range
  if (gap_end <= search_base) {
    return false;
  }
  if (gap_beg < search_base) {
    gap_beg = search_base;
  }

  DEBUG_ASSERT(gap_end > gap_beg);

  LTRACEF_LEVEL(2, "search base %#" PRIxPTR " gap_beg %#" PRIxPTR " end %#" PRIxPTR "\n",
                search_base, gap_beg, gap_end);

  prev_arch_mmu_flags = (prev.IsValid() && prev->is_mapping())
                            ? prev->as_vm_mapping()->arch_mmu_flags()
                            : ARCH_MMU_FLAG_INVALID;

  next_arch_mmu_flags = (next.IsValid() && next->is_mapping())
                            ? next->as_vm_mapping()->arch_mmu_flags()
                            : ARCH_MMU_FLAG_INVALID;

  *pva = aspace_->arch_aspace().PickSpot(gap_beg, prev_arch_mmu_flags, gap_end, next_arch_mmu_flags,
                                         align, region_size, arch_mmu_flags);
  if (*pva < gap_beg) {
    goto not_found;  // address wrapped around
  }

  if (*pva < gap_end && ((gap_end - *pva + 1) >= region_size)) {
    // we have enough room
    return true;  // found spot, stop search
  }

next_gap:
  return false;  // continue search

not_found:
  *pva = -1;
  return true;  // not_found: stop search
}

bool VmAddressRegion::EnumerateChildrenLocked(VmEnumerator* ve, uint depth) {
  canary_.Assert();
  DEBUG_ASSERT(ve != nullptr);
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());

  const uint min_depth = depth;
  for (auto itr = subregions_.begin(), end = subregions_.end(); itr != end;) {
    DEBUG_ASSERT(itr->IsAliveLocked());
    auto curr = itr++;
    VmAddressRegion* up = curr->parent_;

    if (curr->is_mapping()) {
      VmMapping* mapping = curr->as_vm_mapping().get();
      DEBUG_ASSERT(mapping != nullptr);
      if (!ve->OnVmMapping(mapping, this, depth)) {
        return false;
      }
    } else {
      VmAddressRegion* vmar = curr->as_vm_address_region().get();
      DEBUG_ASSERT(vmar != nullptr);
      if (!ve->OnVmAddressRegion(vmar, depth)) {
        return false;
      }
      if (!vmar->subregions_.is_empty()) {
        // If the sub-VMAR is not empty, iterate through its children.
        itr = vmar->subregions_.begin();
        end = vmar->subregions_.end();
        depth++;
        continue;
      }
    }
    if (depth > min_depth && itr == end) {
      // If we are at a depth greater than the minimum, and have reached
      // the end of a sub-VMAR range, we ascend and continue iteration.
      do {
        itr = up->subregions_.upper_bound(curr->base());
        if (itr.IsValid()) {
          break;
        }
        up = up->parent_;
      } while (depth-- != min_depth);
      if (!itr.IsValid()) {
        // If we have reached the end after ascending all the way up,
        // break out of the loop.
        break;
      }
      end = up->subregions_.end();
    }
  }
  return true;
}

bool VmAddressRegion::has_parent() const {
  Guard<fbl::Mutex> guard{aspace_->lock()};
  return parent_ != nullptr;
}

void VmAddressRegion::Dump(uint depth, bool verbose) const {
  canary_.Assert();
  for (uint i = 0; i < depth; ++i) {
    printf("  ");
  }
  printf("vmar %p [%#" PRIxPTR " %#" PRIxPTR "] sz %#zx ref %d '%s'\n", this, base_,
         base_ + size_ - 1, size_, ref_count_debug(), name_);
  for (const auto& child : subregions_) {
    child.Dump(depth + 1, verbose);
  }
}

void VmAddressRegion::Activate() {
  DEBUG_ASSERT(state_ == LifeCycleState::NOT_READY);
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());

  state_ = LifeCycleState::ALIVE;
  parent_->subregions_.insert(fbl::RefPtr<VmAddressRegionOrMapping>(this));
}

zx_status_t VmAddressRegion::RangeOp(uint32_t op, vaddr_t base, size_t size,
                                     user_inout_ptr<void> buffer, size_t buffer_size) {
  canary_.Assert();

  if (buffer || buffer_size) {
    return ZX_ERR_INVALID_ARGS;
  }

  size = ROUNDUP(size, PAGE_SIZE);
  if (size == 0 || !IS_PAGE_ALIGNED(base)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<fbl::Mutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  if (!is_in_range(base, size)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (subregions_.is_empty()) {
    return ZX_ERR_BAD_STATE;
  }

  // Don't allow any operations on the vDSO code mapping.
  // TODO(39860): Factor this out into a common helper.
  if (aspace_->vdso_code_mapping_ && Intersects(aspace_->vdso_code_mapping_->base(),
                                                aspace_->vdso_code_mapping_->size(), base, size)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  const vaddr_t end_addr = base + size;
  auto end = subregions_.lower_bound(end_addr);
  auto begin = UpperBoundInternalLocked(base);

  for (auto curr = begin; curr != end; curr++) {
    // TODO(39861): Allow the |op| range to include child VMARs.
    if (!curr->is_mapping()) {
      return ZX_ERR_BAD_STATE;
    }

    auto mapping = curr->as_vm_mapping();
    fbl::RefPtr<VmObject> vmo = mapping->vmo_locked();
    uint64_t vmo_offset = mapping->object_offset();

    // The |op| range must not include unmapped regions.
    if (base < curr->base()) {
      return ZX_ERR_BAD_STATE;
    }

    const vaddr_t curr_end = curr->base() + curr->size();
    const vaddr_t op_end = fbl::min(curr_end, end_addr);
    const uint64_t op_offset = (base - curr->base()) + vmo_offset;
    const size_t op_size = op_end - base;
    base = op_end;

    switch (op) {
      case ZX_VMO_OP_DECOMMIT: {
        // Decommit zeroes pages of the VMO, equivalent to writing to it.
        // the mapping is currently writable, or could be made writable.
        if (!mapping->is_valid_mapping_flags(ARCH_MMU_FLAG_PERM_WRITE)) {
          return ZX_ERR_ACCESS_DENIED;
        }
        zx_status_t result = vmo->DecommitRange(op_offset, op_size);
        if (result != ZX_OK) {
          return result;
        }
        break;
      }
      default:
        return ZX_ERR_NOT_SUPPORTED;
    };
  }

  // The |op| range must not have an unmapped region at the end.
  if (base != end_addr) {
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

  Guard<fbl::Mutex> guard{aspace_->lock()};
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

  Guard<fbl::Mutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  return UnmapInternalLocked(base, size, true /* can_destroy_regions */,
                             true /* allow_partial_vmar */);
}

VmAddressRegion::ChildList::iterator VmAddressRegion::UpperBoundInternalLocked(vaddr_t base) {
  // Find the first region with a base greater than *base*.  If a region
  // exists for *base*, it will be immediately before it.
  auto itr = --subregions_.upper_bound(base);
  if (!itr.IsValid()) {
    itr = subregions_.begin();
  } else if (base >= itr->base() + itr->size()) {
    // If *base* isn't in this region, ignore it.
    ++itr;
  }
  return itr;
}

zx_status_t VmAddressRegion::UnmapInternalLocked(vaddr_t base, size_t size,
                                                 bool can_destroy_regions,
                                                 bool allow_partial_vmar) {
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());

  if (!is_in_range(base, size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (subregions_.is_empty()) {
    return ZX_OK;
  }

  // Any unmap spanning the vDSO code mapping is verboten.
  if (aspace_->vdso_code_mapping_ && Intersects(aspace_->vdso_code_mapping_->base(),
                                                aspace_->vdso_code_mapping_->size(), base, size)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  const vaddr_t end_addr = base + size;
  auto end = subregions_.lower_bound(end_addr);
  auto begin = UpperBoundInternalLocked(base);

  if (!allow_partial_vmar) {
    // Check if we're partially spanning a subregion, or aren't allowed to
    // destroy regions and are spanning a region, and bail if we are.
    for (auto itr = begin; itr != end; ++itr) {
      const vaddr_t itr_end = itr->base() + itr->size();
      if (!itr->is_mapping() &&
          (!can_destroy_regions || itr->base() < base || itr_end > end_addr)) {
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }

  bool at_top = true;
  for (auto itr = begin; itr != end;) {
    // Create a copy of the iterator, in case we destroy this element
    auto curr = itr++;
    // Stash a copy of curr->base() as we may need to use this value to ascend back to the parent
    // VMAR *after* having destroyed curr.
    uint64_t curr_base = curr->base();
    VmAddressRegion* up = curr->parent_;

    if (curr->is_mapping()) {
      const vaddr_t curr_end = curr->base() + curr->size();
      const vaddr_t unmap_base = fbl::max(curr->base(), base);
      const vaddr_t unmap_end = fbl::min(curr_end, end_addr);
      const size_t unmap_size = unmap_end - unmap_base;

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
        if (!vmar->subregions_.is_empty()) {
          begin = vmar->UpperBoundInternalLocked(base);
          end = vmar->subregions_.lower_bound(end_addr);
          itr = begin;
          at_top = false;
        }
      } else if (unmap_base == curr->base() && unmap_size == curr->size()) {
        __UNUSED zx_status_t status = curr->DestroyLocked();
        DEBUG_ASSERT(status == ZX_OK);
      }
    }

    if (allow_partial_vmar && !at_top && itr == end) {
      // If partial VMARs are allowed, and we have reached the end of a
      // sub-VMAR range, we ascend and continue iteration.
      do {
        // Use the stashed curr_base as if curr was a mapping we may have destroyed it.
        begin = up->subregions_.upper_bound(curr_base);
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
      end = up->subregions_.lower_bound(end_addr);
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

  Guard<fbl::Mutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  if (!is_in_range(base, size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (subregions_.is_empty()) {
    return ZX_ERR_NOT_FOUND;
  }

  const vaddr_t end_addr = base + size;
  const auto end = subregions_.lower_bound(end_addr);

  // Find the first region with a base greater than *base*.  If a region
  // exists for *base*, it will be immediately before it.  If *base* isn't in
  // that entry, bail since it's unmapped.
  auto begin = --subregions_.upper_bound(base);
  if (!begin.IsValid() || begin->base() + begin->size() <= base) {
    return ZX_ERR_NOT_FOUND;
  }

  // Check if we're overlapping a subregion, or a part of the range is not
  // mapped, or the new permissions are invalid for some mapping in the range.
  vaddr_t last_mapped = begin->base();
  for (auto itr = begin; itr != end; ++itr) {
    if (!itr->is_mapping()) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (itr->base() != last_mapped) {
      return ZX_ERR_NOT_FOUND;
    }
    if (!itr->is_valid_mapping_flags(new_arch_mmu_flags)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    if (itr->as_vm_mapping() == aspace_->vdso_code_mapping_) {
      return ZX_ERR_ACCESS_DENIED;
    }

    last_mapped = itr->base() + itr->size();
  }
  if (last_mapped < base + size) {
    return ZX_ERR_NOT_FOUND;
  }

  for (auto itr = begin; itr != end;) {
    DEBUG_ASSERT(itr->is_mapping());

    auto next = itr;
    ++next;

    const vaddr_t curr_end = itr->base() + itr->size();
    const vaddr_t protect_base = fbl::max(itr->base(), base);
    const vaddr_t protect_end = fbl::min(curr_end, end_addr);
    const size_t protect_size = protect_end - protect_base;

    zx_status_t status =
        itr->as_vm_mapping()->ProtectLocked(protect_base, protect_size, new_arch_mmu_flags);
    if (status != ZX_OK) {
      // TODO(teisenbe): Try to work out a way to guarantee success, or
      // provide a full unwind?
      return status;
    }

    itr = ktl::move(next);
  }

  return ZX_OK;
}

template <typename F>
void VmAddressRegion::ForEachGap(F func, uint8_t align_pow2) {
  const vaddr_t align = 1UL << align_pow2;

  // Scan the regions list to find the gap to the left of each region.  We
  // round up the end of the previous region to the requested alignment, so
  // all gaps reported will be for aligned ranges.
  vaddr_t prev_region_end = ROUNDUP(base_, align);
  for (const auto& region : subregions_) {
    if (region.base() > prev_region_end) {
      const size_t gap = region.base() - prev_region_end;
      if (!func(prev_region_end, gap)) {
        return;
      }
    }
    prev_region_end = ROUNDUP(region.base() + region.size(), align);
  }

  // Grab the gap to the right of the last region (note that if there are no
  // regions, this handles reporting the VMAR's whole span as a gap).
  const vaddr_t end = base_ + size_;
  if (end > prev_region_end) {
    const size_t gap = end - prev_region_end;
    func(prev_region_end, gap);
  }
}

namespace {

// Compute the number of allocation spots that satisfy the alignment within the
// given range size, for a range that has a base that satisfies the alignment.
constexpr size_t AllocationSpotsInRange(size_t range_size, size_t alloc_size, uint8_t align_pow2) {
  return ((range_size - alloc_size) >> align_pow2) + 1;
}

}  // namespace

// Perform allocations for VMARs. This allocator works by choosing uniformly at random from a set of
// positions that could satisfy the allocation. The set of positions are the 'left' most positions
// of the address space and are capped by the address entropy limit. The entropy limit is retrieved
// from the address space, and can vary based on whether the user has requested compact allocations
// or not.
zx_status_t VmAddressRegion::AllocSpotLocked(size_t size, uint8_t align_pow2, uint arch_mmu_flags,
                                             vaddr_t* spot) {
  canary_.Assert();
  DEBUG_ASSERT(size > 0 && IS_PAGE_ALIGNED(size));
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());
  DEBUG_ASSERT(spot);

  LTRACEF_LEVEL(2, "aspace %p size 0x%zx align %hhu\n", this, size, align_pow2);

  align_pow2 = fbl::max(align_pow2, static_cast<uint8_t>(PAGE_SIZE_SHIFT));
  const vaddr_t align = 1UL << align_pow2;

  // Ensure our candidate calculation shift will not overflow.
  const uint8_t entropy = aspace_->AslrEntropyBits(flags_ & VMAR_FLAG_COMPACT);
  DEBUG_ASSERT(entropy < sizeof(size_t) * 8);
  // Calculate the number of spaces that we can fit this allocation in.
  size_t candidate_spaces = 0;
  // This is the maximum number of spaces we need to consider based on our desired entropy.
  const size_t max_candidate_spaces = 1ul << entropy;
  ForEachGap(
      [align, align_pow2, size, &candidate_spaces, &max_candidate_spaces](vaddr_t gap_base,
                                                                          size_t gap_len) -> bool {
        DEBUG_ASSERT(IS_ALIGNED(gap_base, align));
        if (gap_len >= size) {
          candidate_spaces += AllocationSpotsInRange(gap_len, size, align_pow2);
        }
        // Return early if we've already found more spaces than we will be considering.
        if (candidate_spaces >= max_candidate_spaces) {
          return false;
        }
        return true;
      },
      align_pow2);

  if (candidate_spaces == 0) {
    return ZX_ERR_NO_MEMORY;
  }

  // Cap our candidate spaces to our entropy limit.
  if (candidate_spaces > max_candidate_spaces) {
    candidate_spaces = max_candidate_spaces;
  }

  // Choose the index of the allocation to use.
  //
  // Avoid calling the PRNG when ASLR is disabled, as it won't be initialized.
  size_t selected_index;
  if (candidate_spaces > 1) {
    DEBUG_ASSERT(aspace_->is_aslr_enabled());
    selected_index = aspace_->AslrPrng().RandInt(candidate_spaces);
  } else {
    selected_index = 0;
  }
  DEBUG_ASSERT(selected_index < candidate_spaces);

  // Find which allocation we picked.
  vaddr_t alloc_spot = static_cast<vaddr_t>(-1);
  ForEachGap(
      [align_pow2, size, &alloc_spot, &selected_index](vaddr_t gap_base, size_t gap_len) -> bool {
        if (gap_len < size) {
          return true;
        }

        const size_t spots = AllocationSpotsInRange(gap_len, size, align_pow2);
        if (selected_index < spots) {
          alloc_spot = gap_base + (selected_index << align_pow2);
          return false;
        }
        selected_index -= spots;
        return true;
      },
      align_pow2);
  ASSERT(alloc_spot != static_cast<vaddr_t>(-1));
  ASSERT(IS_ALIGNED(alloc_spot, align));

  // Sanity check that the allocation fits.
  auto after_iter = subregions_.upper_bound(alloc_spot + size - 1);
  auto before_iter = after_iter;

  if (after_iter == subregions_.begin() || subregions_.size() == 0) {
    before_iter = subregions_.end();
  } else {
    --before_iter;
  }

  ASSERT(before_iter == subregions_.end() || before_iter.IsValid());

  if (CheckGapLocked(before_iter, after_iter, spot, alloc_spot, align, size, 0, arch_mmu_flags) &&
      *spot != static_cast<vaddr_t>(-1)) {
    return ZX_OK;
  }
  panic("Unexpected allocation failure\n");
}
