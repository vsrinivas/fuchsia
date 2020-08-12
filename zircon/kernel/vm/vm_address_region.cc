// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/vm_address_region.h"

#include <align.h>
#include <assert.h>
#include <err.h>
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
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

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

  Guard<Mutex> guard{aspace_->lock()};
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
        return OverwriteVmMapping(new_base, size, vmar_flags, vmo, vmo_offset, arch_mmu_flags, out);
      }
      return ZX_ERR_NO_MEMORY;
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
    vmar = fbl::AdoptRef(new (&ac) VmMapping(*this, new_base, size, vmar_flags, ktl::move(vmo),
                                             is_upper_bound ? 0 : vmo_offset, arch_mmu_flags));
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
                     VMAR_FLAG_OFFSET_IS_UPPER_LIMIT)) {
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
    while (!cur->subregions_.IsEmpty() && !child_region) {
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
        DEBUG_ASSERT(cur->in_subregion_tree());
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
  Guard<Mutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return nullptr;
  }
  return subregions_.FindRegion(addr);
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
  while (auto next = vmar->subregions_.FindRegion(va)) {
    if (next->is_mapping()) {
      return next->PageFault(va, pf_flags, page_request);
    }
    vmar = next->as_vm_address_region();
  }

  return ZX_ERR_NOT_FOUND;
}

bool VmAddressRegion::CheckGapLocked(VmAddressRegionOrMapping* prev, VmAddressRegionOrMapping* next,
                                     vaddr_t* pva, vaddr_t search_base, vaddr_t align,
                                     size_t region_size, size_t min_gap, uint arch_mmu_flags) {
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());

  vaddr_t gap_beg;  // first byte of a gap
  vaddr_t gap_end;  // last byte of a gap

  uint prev_arch_mmu_flags;
  uint next_arch_mmu_flags;

  DEBUG_ASSERT(pva);

  // compute the starting address of the gap
  if (prev != nullptr) {
    if (add_overflow(prev->base(), prev->size(), &gap_beg) ||
        add_overflow(gap_beg, min_gap, &gap_beg)) {
      goto not_found;
    }
  } else {
    gap_beg = base_;
  }

  // compute the ending address of the gap
  if (next != nullptr) {
    if (gap_beg == next->base()) {
      goto next_gap;  // no gap between regions
    }
    if (sub_overflow(next->base(), 1, &gap_end) || sub_overflow(gap_end, min_gap, &gap_end)) {
      goto not_found;
    }
  } else {
    if (gap_beg - base_ == size_) {
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

  prev_arch_mmu_flags = (prev != nullptr && prev->is_mapping())
                            ? prev->as_vm_mapping()->arch_mmu_flags()
                            : ARCH_MMU_FLAG_INVALID;

  next_arch_mmu_flags = (next != nullptr && next->is_mapping())
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
      if (!vmar->subregions_.IsEmpty()) {
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
        itr = up->subregions_.UpperBound(curr->base());
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
  Guard<Mutex> guard{aspace_->lock()};
  return parent_ != nullptr;
}

void VmAddressRegion::Dump(uint depth, bool verbose) const {
  canary_.Assert();
  for (uint i = 0; i < depth; ++i) {
    printf("  ");
  }
  printf("vmar %p [%#" PRIxPTR " %#" PRIxPTR "] sz %#zx ref %d '%s'\n", this, base_,
         base_ + (size_ - 1), size_, ref_count_debug(), name_);
  for (const auto& child : subregions_) {
    child.Dump(depth + 1, verbose);
  }
}

void VmAddressRegion::Activate() {
  DEBUG_ASSERT(state_ == LifeCycleState::NOT_READY);
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());

  state_ = LifeCycleState::ALIVE;
  parent_->subregions_.InsertRegion(fbl::RefPtr<VmAddressRegionOrMapping>(this));
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

  Guard<Mutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  if (!is_in_range(base, size)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (subregions_.IsEmpty()) {
    return ZX_ERR_BAD_STATE;
  }

  // Don't allow any operations on the vDSO code mapping.
  // TODO(fxbug.dev/39860): Factor this out into a common helper.
  if (aspace_->vdso_code_mapping_ && Intersects(aspace_->vdso_code_mapping_->base(),
                                                aspace_->vdso_code_mapping_->size(), base, size)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // Last byte of the range.
  vaddr_t end_addr_byte;
  DEBUG_ASSERT(size > 0);
  bool overflowed = add_overflow(base, size - 1, &end_addr_byte);
  ASSERT(!overflowed);
  auto end = subregions_.UpperBound(end_addr_byte);
  auto begin = subregions_.IncludeOrHigher(base);
  vaddr_t op_end_byte = 0;

  for (auto curr = begin; curr != end; curr++) {
    // TODO(fxbug.dev/39861): Allow the |op| range to include child VMARs.
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
    // Last byte of the current region.
    vaddr_t curr_end_byte = 0;
    DEBUG_ASSERT(curr->size() > 0);
    overflowed = add_overflow(curr->base(), curr->size() - 1, &curr_end_byte);
    op_end_byte = ktl::min(curr_end_byte, end_addr_byte);
    const uint64_t op_offset = (base - curr->base()) + vmo_offset;
    size_t op_size = 0;
    overflowed = add_overflow(op_end_byte - base, 1, &op_size);
    ASSERT(!overflowed);

    switch (op) {
      case ZX_VMAR_OP_DECOMMIT: {
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
      case ZX_VMAR_OP_MAP_RANGE: {
        LTRACEF_LEVEL(2, "MapRange: op_offset=0x%zx op_size=0x%zx\n", op_offset, op_size);
        const auto result = mapping->MapRangeLocked(op_offset, op_size, false);
        if (result != ZX_OK) {
          // TODO(fxbug.dev/46881): ZX_ERR_INTERNAL is not meaningful to userspace.
          // For now, translate to ZX_ERR_NOT_FOUND.
          return result == ZX_ERR_INTERNAL ? ZX_ERR_NOT_FOUND : result;
        }
        break;
      }
      default:
        return ZX_ERR_NOT_SUPPORTED;
    };
    vaddr_t next_base = 0;
    if (!add_overflow(op_end_byte, 1, &next_base)) {
      base = next_base;
    } else {
      // If this happens, there must not be a next sub region but we break anyway to make sure
      // we would not infinite loop.
      break;
    }
  }

  // The |op| range must not have an unmapped region at the end.
  if (op_end_byte != end_addr_byte) {
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

  Guard<Mutex> guard{aspace_->lock()};
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

  Guard<Mutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  return UnmapInternalLocked(base, size, true /* can_destroy_regions */,
                             true /* allow_partial_vmar */);
}

zx_status_t VmAddressRegion::UnmapInternalLocked(vaddr_t base, size_t size,
                                                 bool can_destroy_regions,
                                                 bool allow_partial_vmar) {
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());

  if (!is_in_range(base, size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (subregions_.IsEmpty()) {
    return ZX_OK;
  }

  // Any unmap spanning the vDSO code mapping is verboten.
  if (aspace_->vdso_code_mapping_ && Intersects(aspace_->vdso_code_mapping_->base(),
                                                aspace_->vdso_code_mapping_->size(), base, size)) {
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
      curr_base = curr->base();
      // The parent will keep living even if we destroy curr so can place that in the outer scope.
      up = curr->parent_;

      if (curr->is_mapping()) {
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

  Guard<Mutex> guard{aspace_->lock()};
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
  const auto end = subregions_.UpperBound(end_addr_byte);

  // Find the first region with a base greater than *base*.  If a region
  // exists for *base*, it will be immediately before it.  If *base* isn't in
  // that entry, bail since it's unmapped.
  auto begin = --subregions_.UpperBound(base);
  if (!begin.IsValid() || begin->size() <= base - begin->base()) {
    return ZX_ERR_NOT_FOUND;
  }

  // Check if we're overlapping a subregion, or a part of the range is not
  // mapped, or the new permissions are invalid for some mapping in the range.

  // The last byte of the last mapped region.
  vaddr_t last_mapped_byte = begin->base();
  if (begin->base() != 0) {
    last_mapped_byte--;
  }
  for (auto itr = begin; itr != end; ++itr) {
    if (!itr->is_mapping()) {
      return ZX_ERR_INVALID_ARGS;
    }
    vaddr_t current_begin = 0;
    // This would not overflow because previous region end + 1 would not overflow.
    overflowed = add_overflow(last_mapped_byte, 1, &current_begin);
    ASSERT(!overflowed);
    if (itr->base() != current_begin) {
      return ZX_ERR_NOT_FOUND;
    }
    if (!itr->is_valid_mapping_flags(new_arch_mmu_flags)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    if (itr->as_vm_mapping() == aspace_->vdso_code_mapping_) {
      return ZX_ERR_ACCESS_DENIED;
    }
    overflowed = add_overflow(itr->base(), itr->size() - 1, &last_mapped_byte);
    ASSERT(!overflowed);
  }
  if (last_mapped_byte < end_addr_byte) {
    return ZX_ERR_NOT_FOUND;
  }

  for (auto itr = begin; itr != end;) {
    DEBUG_ASSERT(itr->is_mapping());

    auto next = itr;
    ++next;

    // The last byte of the current region.
    vaddr_t curr_end_byte = 0;
    overflowed = add_overflow(itr->base(), itr->size() - 1, &curr_end_byte);
    ASSERT(!overflowed);
    const vaddr_t protect_base = ktl::max(itr->base(), base);
    const vaddr_t protect_end_byte = ktl::min(curr_end_byte, end_addr_byte);
    size_t protect_size;
    overflowed = add_overflow(protect_end_byte - protect_base, 1, &protect_size);
    ASSERT(!overflowed);

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

namespace {

// Compute the number of allocation spots that satisfy the alignment within the
// given range size, for a range that has a base that satisfies the alignment.
constexpr size_t AllocationSpotsInRange(size_t range_size, size_t alloc_size, uint8_t align_pow2) {
  return ((range_size - alloc_size) >> align_pow2) + 1;
}

// Returns the size of the given range clamped to the given upper limit. The base
// of the range must be within the upper limit.
constexpr size_t ClampRange(vaddr_t range_base, size_t range_size, vaddr_t upper_limit) {
  DEBUG_ASSERT(range_base <= upper_limit);
  const size_t range_limit = range_base + range_size;
  return range_limit <= upper_limit ? range_size : range_size - (range_limit - upper_limit);
}

}  // namespace

// Perform allocations for VMARs. This allocator works by choosing uniformly at random from a set of
// positions that could satisfy the allocation. The set of positions are the 'left' most positions
// of the address space and are capped by the address entropy limit. The entropy limit is retrieved
// from the address space, and can vary based on whether the user has requested compact allocations
// or not.
zx_status_t VmAddressRegion::AllocSpotLocked(size_t size, uint8_t align_pow2, uint arch_mmu_flags,
                                             vaddr_t* spot, vaddr_t upper_limit) {
  canary_.Assert();
  DEBUG_ASSERT(size > 0 && IS_PAGE_ALIGNED(size));
  DEBUG_ASSERT(aspace_->lock()->lock().IsHeld());
  DEBUG_ASSERT(spot);

  LTRACEF_LEVEL(2, "aspace %p size 0x%zx align %hhu upper_limit 0x%lx\n", this, size, align_pow2,
                upper_limit);

  align_pow2 = ktl::max(align_pow2, static_cast<uint8_t>(PAGE_SIZE_SHIFT));
  const vaddr_t align = 1UL << align_pow2;
  // Ensure our candidate calculation shift will not overflow.
  const uint8_t entropy = aspace_->AslrEntropyBits(flags_ & VMAR_FLAG_COMPACT);
  vaddr_t alloc_spot = 0;
  crypto::PRNG* prng = nullptr;
  if (aspace_->is_aslr_enabled()) {
    prng = &aspace_->AslrPrng();
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
  if (CheckGapLocked(before, after, spot, alloc_spot, align, size, 0, arch_mmu_flags) &&
      *spot != static_cast<vaddr_t>(-1)) {
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
  fbl::RefPtr<VmObject> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, 0, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  vmo->set_name(name, strlen(name));
  // allocate a region and put it in the aspace list
  fbl::RefPtr<VmMapping> r(nullptr);
  // Here we use permissive arch_mmu_flags so that the following Protect call would actually
  // call arch_aspace().Protect to change the mmu_flags in PTE.
  status = CreateVmMapping(
      offset, size, 0, VMAR_FLAG_SPECIFIC, vmo, 0,
      ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE, name, &r);
  if (status != ZX_OK) {
    return status;
  }
  return r->Protect(base, size, arch_mmu_flags);
}

fbl::RefPtr<VmAddressRegionOrMapping> RegionList::RemoveRegion(VmAddressRegionOrMapping* region) {
  return regions_.erase(*region);
}

void RegionList::InsertRegion(fbl::RefPtr<VmAddressRegionOrMapping> region) {
  regions_.insert(region);
}

fbl::RefPtr<VmAddressRegionOrMapping> RegionList::FindRegion(vaddr_t addr) const {
  // Find the first region with a base greater than *addr*.  If a region
  // exists for *addr*, it will be immediately before it.
  auto itr = --regions_.upper_bound(addr);
  if (!itr.IsValid()) {
    return nullptr;
  }
  // Subregion size should never be zero unless during unmapping which should never overlap with
  // this operation.
  DEBUG_ASSERT(itr->size() > 0);
  vaddr_t region_end;
  bool overflowed = add_overflow(itr->base(), itr->size() - 1, &region_end);
  ASSERT(!overflowed);
  if (itr->base() > addr || addr > region_end) {
    return nullptr;
  }

  return itr.CopyPointer();
}

RegionList::ChildList::iterator RegionList::IncludeOrHigher(vaddr_t base) {
  // Find the first region with a base greater than *base*.  If a region
  // exists for *base*, it will be immediately before it.
  auto itr = regions_.upper_bound(base);
  itr--;
  if (!itr.IsValid()) {
    itr = regions_.begin();
  } else if (base >= itr->base() && base - itr->base() >= itr->size()) {
    // If *base* isn't in this region, ignore it.
    ++itr;
  }
  return itr;
}

RegionList::ChildList::iterator RegionList::UpperBound(vaddr_t base) {
  return regions_.upper_bound(base);
}

bool RegionList::IsRangeAvailable(vaddr_t base, size_t size) const {
  DEBUG_ASSERT(size > 0);

  // Find the first region with base > *base*.  Since subregions_ has no
  // overlapping elements, we just need to check this one and the prior
  // child.

  auto prev = regions_.upper_bound(base);
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

  if (next.IsValid() && next != regions_.end()) {
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

void RegionList::FindAllocSpotInGaps(size_t size, uint8_t align_pow2, vaddr_t selected_index,
                                     vaddr_t parent_base, vaddr_t parent_size,
                                     RegionList::AllocSpotInfo* alloc_spot_info,
                                     vaddr_t upper_limit) const {
  const vaddr_t align = 1UL << align_pow2;
  // candidate_spot_count is the number of available slot that we could allocate if we have not
  // found the spot with index |selected_index| to allocate.
  size_t candidate_spot_count = 0;
  // Found indicates whether we have found the spot with index |selected_indexes|.
  bool found = false;
  // alloc_spot is the virtual start address of the spot to allocate if we find one.
  vaddr_t alloc_spot = 0;
  ForEachGap(
      [align, align_pow2, size, upper_limit, &candidate_spot_count, &selected_index, &alloc_spot,
       &found](vaddr_t gap_base, size_t gap_len) -> bool {
        DEBUG_ASSERT(IS_ALIGNED(gap_base, align));
        if (gap_len < size || gap_base + size > upper_limit) {
          // Ignore gap that is too small or out of range.
          return true;
        }
        const size_t clamped_len = ClampRange(gap_base, gap_len, upper_limit);
        const size_t spots = AllocationSpotsInRange(clamped_len, size, align_pow2);
        candidate_spot_count += spots;

        if (selected_index < spots) {
          // If we are able to find the spot with index |selected_indexes| in this gap, then we have
          // found our pick.
          found = true;
          alloc_spot = gap_base + (selected_index << align_pow2);
          return false;
        }
        selected_index -= spots;
        return true;
      },
      align_pow2, parent_base, parent_size);
  alloc_spot_info->found = found;
  alloc_spot_info->alloc_spot = alloc_spot;
  alloc_spot_info->candidate_spot_count = candidate_spot_count;
  return;
}

zx_status_t RegionList::GetAllocSpot(vaddr_t* alloc_spot, uint8_t align_pow2, uint8_t entropy,
                                     size_t size, vaddr_t parent_base, size_t parent_size,
                                     crypto::PRNG* prng, vaddr_t upper_limit) const {
  DEBUG_ASSERT(entropy < sizeof(size_t) * 8);
  const vaddr_t align = 1UL << align_pow2;
  // This is the maximum number of spaces we need to consider based on our desired entropy.
  const size_t max_candidate_spaces = 1ul << entropy;
  vaddr_t selected_index = 0;
  if (prng != nullptr) {
    // We first pick a index in [0, max_candidate_spaces] and hope to find the index.
    // If the number of available spots is less than selected_index, alloc_spot_info.founds would
    // be false. This means that selected_index is too large, we have to pick again in a smaller
    // range and try again.
    //
    // Note that this is mathematically equal to randomly pick a spot within
    // [0, candidate_spot_count] if selected_index <= candidate_spot_count.
    //
    // Prove as following:
    // Define M = candidate_spot_count
    // Define N = max_candidate_spaces (M < N, otherwise we can randomly allocate any spot from
    // [0, max_candidate_spaces], thus allocate a specific slot has (1 / N) probability).
    // Define slot X0 where X0 belongs to [1, M].
    // Define event A: randomly pick a slot X in [1, N], N = X0.
    // Define event B: randomly pick a slot X in [1, N], N belongs to [1, M].
    // Define event C: randomly pick a slot X in [1, N], N = X0 when N belongs to [1, M].
    // P(C) = P(A | B)
    // Since when A happens, B definitely happens, so P(AB) = P(A)
    // P(C) = P(A) / P(B) = (1 / N) / (M / N) = (1 / M)
    // which is equal to the probability of picking a specific spot in [0, M].
    selected_index = prng->RandInt(max_candidate_spaces);
  }

  AllocSpotInfo alloc_spot_info;
  FindAllocSpotInGaps(size, align_pow2, selected_index, parent_base, parent_size, &alloc_spot_info,
                      upper_limit);
  size_t candidate_spot_count = alloc_spot_info.candidate_spot_count;
  if (candidate_spot_count == 0) {
    DEBUG_ASSERT(!alloc_spot_info.found);
    return ZX_ERR_NO_MEMORY;
  }
  if (!alloc_spot_info.found) {
    if (candidate_spot_count > max_candidate_spaces) {
      candidate_spot_count = max_candidate_spaces;
    }
    // If the number of candidate spaces is less than the index we want, let's pick again from the
    // range for available spaces.
    DEBUG_ASSERT(prng);
    selected_index = prng->RandInt(candidate_spot_count);
    FindAllocSpotInGaps(size, align_pow2, selected_index, parent_base, parent_size,
                        &alloc_spot_info, upper_limit);
  }
  DEBUG_ASSERT(alloc_spot_info.found);
  *alloc_spot = alloc_spot_info.alloc_spot;
  ASSERT(IS_ALIGNED(*alloc_spot, align));

  return ZX_OK;
}

template <typename F>
void RegionList::ForEachGap(F func, uint8_t align_pow2, vaddr_t parent_base,
                            size_t parent_size) const {
  const vaddr_t align = 1UL << align_pow2;

  // Scan the regions list to find the gap to the left of each region.  We
  // round up the end of the previous region to the requested alignment, so
  // all gaps reported will be for aligned ranges.
  vaddr_t prev_region_end = ROUNDUP(parent_base, align);
  for (const auto& region : regions_) {
    if (region.base() > prev_region_end) {
      const size_t gap = region.base() - prev_region_end;
      if (!func(prev_region_end, gap)) {
        return;
      }
    }
    if (add_overflow(region.base(), region.size(), &prev_region_end)) {
      // This region is already the last region.
      return;
    }
    prev_region_end = ROUNDUP(prev_region_end, align);
  }

  // Grab the gap to the right of the last region (note that if there are no
  // regions, this handles reporting the VMAR's whole span as a gap).
  if (parent_size > prev_region_end - parent_base) {
    // This is equal to parent_base + parent_size - prev_region_end, but guarantee no overflow.
    const size_t gap = parent_size - (prev_region_end - parent_base);
    func(prev_region_end, gap);
  }
}
