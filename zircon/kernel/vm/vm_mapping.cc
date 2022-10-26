// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <assert.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <lib/fit/defer.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <ktl/algorithm.h>
#include <ktl/iterator.h>
#include <ktl/move.h>
#include <vm/fault.h>
#include <vm/physmap.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>

#include "vm/vm_address_region.h"
#include "vm_priv.h"

#include <ktl/enforce.h>

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

namespace {

KCOUNTER(vm_mapping_attribution_queries, "vm.attributed_pages.mapping.queries")
KCOUNTER(vm_mapping_attribution_cache_hits, "vm.attributed_pages.mapping.cache_hits")
KCOUNTER(vm_mapping_attribution_cache_misses, "vm.attributed_pages.mapping.cache_misses")
KCOUNTER(vm_mappings_merged, "vm.aspace.mapping.merged_neighbors")
KCOUNTER(vm_mappings_protect_no_write, "vm.aspace.mapping.protect_without_write")

}  // namespace

VmMapping::VmMapping(VmAddressRegion& parent, vaddr_t base, size_t size, uint32_t vmar_flags,
                     fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
                     MappingProtectionRanges&& ranges, Mergeable mergeable)
    : VmAddressRegionOrMapping(base, size, vmar_flags, parent.aspace_.get(), &parent, true),
      object_(ktl::move(vmo)),
      object_offset_(vmo_offset),
      protection_ranges_(ktl::move(ranges)),
      mergeable_(mergeable) {
  LTRACEF("%p aspace %p base %#" PRIxPTR " size %#zx offset %#" PRIx64 "\n", this, aspace_.get(),
          base_, size_, vmo_offset);
}

VmMapping::VmMapping(VmAddressRegion& parent, vaddr_t base, size_t size, uint32_t vmar_flags,
                     fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset, uint arch_mmu_flags,
                     Mergeable mergeable)
    : VmMapping(parent, base, size, vmar_flags, vmo, vmo_offset,
                MappingProtectionRanges(arch_mmu_flags), mergeable) {}

VmMapping::~VmMapping() {
  canary_.Assert();
  LTRACEF("%p aspace %p base %#" PRIxPTR " size %#zx\n", this, aspace_.get(), base_, size_);
}

fbl::RefPtr<VmObject> VmMapping::vmo() const {
  Guard<CriticalMutex> guard{aspace_->lock()};
  return vmo_locked();
}

VmMapping::AttributionCounts VmMapping::AllocatedPagesLocked() const {
  canary_.Assert();

  if (state_ != LifeCycleState::ALIVE) {
    return AttributionCounts{};
  }

  vm_mapping_attribution_queries.Add(1);

  if (!object_->is_paged()) {
    return object_->AttributedPagesInRange(object_offset_locked(), size_);
  }

  // If |object_| is a VmObjectPaged, check if the previously cached value still holds.
  auto object_paged = static_cast<VmObjectPaged*>(object_.get());
  uint64_t vmo_gen_count = object_paged->GetHierarchyGenerationCount();
  uint64_t mapping_gen_count = GetMappingGenerationCountLocked();

  // Return the cached page count if the mapping's generation count and the vmo's generation count
  // have not changed.
  if (cached_page_attribution_.mapping_generation_count == mapping_gen_count &&
      cached_page_attribution_.vmo_generation_count == vmo_gen_count) {
    vm_mapping_attribution_cache_hits.Add(1);
    return cached_page_attribution_.page_counts;
  }

  vm_mapping_attribution_cache_misses.Add(1);

  AttributionCounts page_counts =
      object_paged->AttributedPagesInRange(object_offset_locked(), size_);

  DEBUG_ASSERT(cached_page_attribution_.mapping_generation_count != mapping_gen_count ||
               cached_page_attribution_.vmo_generation_count != vmo_gen_count);
  cached_page_attribution_.mapping_generation_count = mapping_gen_count;
  cached_page_attribution_.vmo_generation_count = vmo_gen_count;
  cached_page_attribution_.page_counts = page_counts;

  return page_counts;
}

void VmMapping::DumpLocked(uint depth, bool verbose) const {
  canary_.Assert();
  for (uint i = 0; i < depth; ++i) {
    printf("  ");
  }
  char vmo_name[32];
  object_->get_name(vmo_name, sizeof(vmo_name));
  printf("map %p [%#" PRIxPTR " %#" PRIxPTR "] sz %#zx state %d mergeable %s\n", this, base_,
         base_ + size_ - 1, size_, (int)state_, mergeable_ == Mergeable::YES ? "true" : "false");
  EnumerateProtectionRangesLocked(base_, size_, [depth](vaddr_t base, size_t len, uint mmu_flags) {
    for (uint i = 0; i < depth + 1; ++i) {
      printf("  ");
    }
    printf(" [%#" PRIxPTR " %#" PRIxPTR "] mmufl %#x\n", base, base + len - 1, mmu_flags);
    return ZX_ERR_NEXT;
  });
  for (uint i = 0; i < depth + 1; ++i) {
    printf("  ");
  }
  AttributionCounts page_counts = object_->AttributedPagesInRange(object_offset_locked(), size_);
  printf("vmo %p/k%" PRIu64 " off %#" PRIx64 " pages (%zu/%zu) ref %d '%s'\n", object_.get(),
         object_->user_id(), object_offset_locked(), page_counts.uncompressed,
         page_counts.compressed, ref_count_debug(), vmo_name);
  if (verbose) {
    object_->Dump(depth + 1, false);
  }
}

zx_status_t VmMapping::Protect(vaddr_t base, size_t size, uint new_arch_mmu_flags) {
  canary_.Assert();
  LTRACEF("%p %#" PRIxPTR " %#x %#x\n", this, base_, flags_, new_arch_mmu_flags);

  if (!IS_PAGE_ALIGNED(base)) {
    return ZX_ERR_INVALID_ARGS;
  }

  size = ROUNDUP(size, PAGE_SIZE);

  Guard<CriticalMutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  if (size == 0 || !is_in_range(base, size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  return ProtectLocked(base, size, new_arch_mmu_flags);
}

// static
zx_status_t VmMapping::ProtectOrUnmap(const fbl::RefPtr<VmAspace>& aspace, vaddr_t base,
                                      size_t size, uint new_arch_mmu_flags) {
  // This can never be used to set a WRITE permission since it does not ask the underlying VMO to
  // perform the copy-on-write step. The underlying VMO might also support dirty tracking, which
  // requires write permission faults in order to track pages as dirty when written.
  ASSERT(!(new_arch_mmu_flags & ARCH_MMU_FLAG_PERM_WRITE));
  // If not removing all permissions do the protect, otherwise skip straight to unmapping the entire
  // region.
  if ((new_arch_mmu_flags & ARCH_MMU_FLAG_PERM_RWX_MASK) != 0) {
    zx_status_t status = aspace->arch_aspace().Protect(base, size / PAGE_SIZE, new_arch_mmu_flags);
    // If the unmap failed and we are allowed to unmap extra portions of the aspace then fall
    // through and unmap, otherwise return with whatever the status is.
    if (likely(status == ZX_OK) || !aspace->EnlargeArchUnmap()) {
      return status;
    }
  }

  return aspace->arch_aspace().Unmap(base, size / PAGE_SIZE, aspace->EnlargeArchUnmap(), nullptr);
}

zx_status_t VmMapping::ProtectLocked(vaddr_t base, size_t size, uint new_arch_mmu_flags) {
  DEBUG_ASSERT(size != 0 && IS_PAGE_ALIGNED(base) && IS_PAGE_ALIGNED(size));

  // Do not allow changing caching
  if (new_arch_mmu_flags & ARCH_MMU_FLAG_CACHE_MASK) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (!is_valid_mapping_flags(new_arch_mmu_flags)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  DEBUG_ASSERT(object_);
  // grab the lock for the vmo
  Guard<CriticalMutex> guard{object_->lock()};

  // Persist our current caching mode. Every protect region will have the same caching mode so we
  // can acquire this from any region.
  new_arch_mmu_flags |= (protection_ranges_.FirstRegionMmuFlags() & ARCH_MMU_FLAG_CACHE_MASK);

  // This will get called by UpdateProtectionRange below for every existing unique protection range
  // that gets changed and allows us to fine tune the protect action based on the previous flags.
  auto protect_callback = [new_arch_mmu_flags, this](vaddr_t base, size_t size,
                                                     uint old_arch_mmu_flags) {
    // Perform an early return if the new and old flags are the same, as there's nothing to be done.
    if (new_arch_mmu_flags == old_arch_mmu_flags) {
      return;
    }

    uint flags = new_arch_mmu_flags;
    // Check if the new flags have the write permission. This is problematic as we cannot just
    // change any existing hardware mappings to have the write permission, as any individual mapping
    // may be the result of a read fault and still need to have a copy-on-write step performed. This
    // could also map a dirty tracked VMO which requires write permission faults to track pages as
    // dirty when written.
    if (new_arch_mmu_flags & ARCH_MMU_FLAG_PERM_WRITE) {
      // Whatever happens, we're not going to be protecting the arch aspace to have write mappings,
      // so this has to be a user aspace so that we can lazily take write faults in the future.
      ASSERT(aspace_->is_user() || aspace_->is_guest_physical());
      flags &= ~ARCH_MMU_FLAG_PERM_WRITE;
      vm_mappings_protect_no_write.Add(1);
      // If the new flags without write permission are the same as the old flags, then skip the
      // protect step since it will be a no-op.
      if (flags == old_arch_mmu_flags) {
        return;
      }
    }

    zx_status_t status = ProtectOrUnmap(aspace_, base, size, flags);
    // If the protect failed then we do not have sufficient information left to rollback in order to
    // return an error, nor can we claim success, so require the protect to have succeeded to
    // continue.
    ASSERT(status == ZX_OK);
  };

  return protection_ranges_.UpdateProtectionRange(base_, size_, base, size, new_arch_mmu_flags,
                                                  protect_callback);
}

zx_status_t VmMapping::Unmap(vaddr_t base, size_t size) {
  LTRACEF("%p %#" PRIxPTR " %zu\n", this, base, size);

  if (!IS_PAGE_ALIGNED(base)) {
    return ZX_ERR_INVALID_ARGS;
  }

  size = ROUNDUP(size, PAGE_SIZE);

  fbl::RefPtr<VmAspace> aspace(aspace_);
  if (!aspace) {
    return ZX_ERR_BAD_STATE;
  }

  Guard<CriticalMutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  if (size == 0 || !is_in_range(base, size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // If we're unmapping everything, destroy this mapping
  if (base == base_ && size == size_) {
    return DestroyLocked();
  }

  return UnmapLocked(base, size);
}

zx_status_t VmMapping::UnmapLocked(vaddr_t base, size_t size) {
  canary_.Assert();
  DEBUG_ASSERT(size != 0 && IS_PAGE_ALIGNED(size) && IS_PAGE_ALIGNED(base));
  DEBUG_ASSERT(base >= base_ && base - base_ < size_);
  DEBUG_ASSERT(size_ - (base - base_) >= size);
  DEBUG_ASSERT(parent_);

  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  AssertHeld(parent_->lock_ref());

  // Should never be unmapping everything, otherwise should destroy.
  DEBUG_ASSERT(base != base_ || size != size_);

  LTRACEF("%p\n", this);

  // Grab the lock for the vmo. This is acquired here so that it is held continuously over both the
  // architectural unmap and the set_size_locked call.
  DEBUG_ASSERT(object_);
  Guard<CriticalMutex> guard{object_->lock()};

  // Check if unmapping from one of the ends
  if (base_ == base || base + size == base_ + size_) {
    LTRACEF("unmapping base %#lx size %#zx\n", base, size);
    zx_status_t status =
        aspace_->arch_aspace().Unmap(base, size / PAGE_SIZE, aspace_->EnlargeArchUnmap(), nullptr);
    if (status != ZX_OK) {
      return status;
    }

    if (base_ == base) {
      DEBUG_ASSERT(size != size_);
      // First remove any protect regions we will no longer need.
      protection_ranges_.DiscardBelow(base_ + size);

      // We need to remove ourselves from tree before updating base_,
      // since base_ is the tree key.
      AssertHeld(parent_->lock_ref());
      fbl::RefPtr<VmAddressRegionOrMapping> ref(parent_->subregions_.RemoveRegion(this));
      base_ += size;
      object_offset_ += size;
      parent_->subregions_.InsertRegion(ktl::move(ref));
    } else {
      // Resize the protection range, will also cause it to discard any protection ranges that are
      // outside the new size.
      protection_ranges_.DiscardAbove(base);
    }

    set_size_locked(size_ - size);
    return ZX_OK;
  }

  // We're unmapping from the center, so we need to split the mapping
  DEBUG_ASSERT(parent_->state_ == LifeCycleState::ALIVE);

  const uint64_t vmo_offset = object_offset_ + (base + size) - base_;
  const vaddr_t new_base = base + size;
  const size_t new_size = (base_ + size_) - new_base;

  // Split off any protection information for the new mapping.
  MappingProtectionRanges new_protect = protection_ranges_.SplitAt(new_base);

  fbl::AllocChecker ac;
  fbl::RefPtr<VmMapping> mapping(
      fbl::AdoptRef(new (&ac) VmMapping(*parent_, new_base, new_size, flags_, object_, vmo_offset,
                                        ktl::move(new_protect), Mergeable::YES)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Unmap the middle segment
  LTRACEF("unmapping base %#lx size %#zx\n", base, size);
  zx_status_t status =
      aspace_->arch_aspace().Unmap(base, size / PAGE_SIZE, aspace_->EnlargeArchUnmap(), nullptr);
  if (status != ZX_OK) {
    return status;
  }

  // Turn us into the left half
  protection_ranges_.DiscardAbove(base);
  set_size_locked(base - base_);
  AssertHeld(mapping->lock_ref());
  mapping->assert_object_lock();
  mapping->ActivateLocked();
  return ZX_OK;
}

bool VmMapping::ObjectRangeToVaddrRange(uint64_t offset, uint64_t len, vaddr_t* base,
                                        uint64_t* virtual_len) const {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));
  DEBUG_ASSERT(base);
  DEBUG_ASSERT(virtual_len);

  // Zero sized ranges are considered to have no overlap.
  if (len == 0) {
    *base = 0;
    *virtual_len = 0;
    return false;
  }

  // compute the intersection of the passed in vmo range and our mapping
  uint64_t offset_new;
  if (!GetIntersect(object_offset_locked_object(), static_cast<uint64_t>(size_), offset, len,
                    &offset_new, virtual_len)) {
    return false;
  }

  DEBUG_ASSERT(*virtual_len > 0 && *virtual_len <= SIZE_MAX);
  DEBUG_ASSERT(offset_new >= object_offset_locked_object());

  LTRACEF("intersection offset %#" PRIx64 ", len %#" PRIx64 "\n", offset_new, *virtual_len);

  // make sure the base + offset is within our address space
  // should be, according to the range stored in base_ + size_
  bool overflowed = add_overflow(base_, offset_new - object_offset_locked_object(), base);
  ASSERT(!overflowed);

  // make sure we're only operating within our window
  ASSERT(*base >= base_);
  ASSERT((*base + *virtual_len - 1) <= (base_ + size_ - 1));

  return true;
}

void VmMapping::AspaceUnmapVmoRangeLocked(uint64_t offset, uint64_t len) const {
  canary_.Assert();

  // NOTE: must be acquired with the vmo lock held, but doesn't need to take
  // the address space lock, since it will not manipulate its location in the
  // vmar tree. However, it must be held in the ALIVE state across this call.
  //
  // Avoids a race with DestroyLocked() since it removes ourself from the VMO's
  // mapping list with the VMO lock held before dropping this state to DEAD. The
  // VMO cant call back to us once we're out of their list.
  DEBUG_ASSERT(get_state_locked_object() == LifeCycleState::ALIVE);

  // |object_| itself is not accessed in this method, and we do not hold the correct lock for it,
  // but we know the object_->lock() is held and so therefore object_ is valid and will not be
  // modified. Therefore it's correct to read object_ here for the purposes of an assert, but cannot
  // be expressed nicely with regular annotations.
  [&]() TA_NO_THREAD_SAFETY_ANALYSIS { DEBUG_ASSERT(object_); }();

  LTRACEF("region %p obj_offset %#" PRIx64 " size %zu, offset %#" PRIx64 " len %#" PRIx64 "\n",
          this, object_offset_locked_object(), size_, offset, len);

  // If we're currently faulting and are responsible for the vmo code to be calling
  // back to us, detect the recursion and abort here.
  // The specific path we're avoiding is if the VMO calls back into us during
  // vmo->LookupPagesLocked() via AspaceUnmapVmoRangeLocked(). If we set this flag we're short
  // circuiting the unmap operation so that we don't do extra work.
  if (unlikely(currently_faulting_)) {
    LTRACEF("recursing to ourself, abort\n");
    return;
  }

  // See if there's an intersect.
  vaddr_t base;
  uint64_t new_len;
  if (!ObjectRangeToVaddrRange(offset, len, &base, &new_len)) {
    return;
  }

  // If this is a kernel mapping then we should not be removing mappings out of the arch aspace,
  // unless this mapping has explicitly opted out of this check.
  DEBUG_ASSERT(aspace_->is_user() || aspace_->is_guest_physical() ||
               flags_ & VMAR_FLAG_DEBUG_DYNAMIC_KERNEL_MAPPING);

  zx_status_t status =
      aspace_->arch_aspace().Unmap(base, new_len / PAGE_SIZE, aspace_->EnlargeArchUnmap(), nullptr);
  ASSERT(status == ZX_OK);
}

void VmMapping::AspaceRemoveWriteVmoRangeLocked(uint64_t offset, uint64_t len) const {
  LTRACEF("region %p obj_offset %#" PRIx64 " size %zu, offset %#" PRIx64 " len %#" PRIx64 "\n",
          this, object_offset_, size_, offset, len);

  canary_.Assert();

  // NOTE: must be acquired with the vmo lock held, but doesn't need to take
  // the address space lock, since it will not manipulate its location in the
  // vmar tree. However, it must be held in the ALIVE state across this call.
  //
  // Avoids a race with DestroyLocked() since it removes ourself from the VMO's
  // mapping list with the VMO lock held before dropping this state to DEAD. The
  // VMO cant call back to us once we're out of their list.
  DEBUG_ASSERT(get_state_locked_object() == LifeCycleState::ALIVE);

  // |object_| itself is not accessed in this method, and we do not hold the correct lock for it,
  // but we know the object_->lock() is held and so therefore object_ is valid and will not be
  // modified. Therefore it's correct to read object_ here for the purposes of an assert, but cannot
  // be expressed nicely with regular annotations.
  [&]() TA_NO_THREAD_SAFETY_ANALYSIS { DEBUG_ASSERT(object_); }();

  // If this doesn't support writing then nothing to be done, as we know we have no write mappings.
  if (!(flags_ & VMAR_FLAG_CAN_MAP_WRITE)) {
    return;
  }

  // See if there's an intersect.
  vaddr_t base;
  uint64_t new_len;
  if (!ObjectRangeToVaddrRange(offset, len, &base, &new_len)) {
    return;
  }

  // If this is a kernel mapping then we should not be modify mappings in the arch aspace,
  // unless this mapping has explicitly opted out of this check.
  DEBUG_ASSERT(aspace_->is_user() || aspace_->is_guest_physical() ||
               flags_ & VMAR_FLAG_DEBUG_DYNAMIC_KERNEL_MAPPING);

  zx_status_t status = ProtectRangesLockedObject().EnumerateProtectionRanges(
      base_, size_, base, new_len, [this](vaddr_t region_base, size_t region_len, uint mmu_flags) {
        // If this range doesn't currently support being writable then we can skip.
        if (!(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE)) {
          return ZX_ERR_NEXT;
        }

        // Build new mmu flags without writing.
        mmu_flags &= ~(ARCH_MMU_FLAG_PERM_WRITE);

        zx_status_t result = ProtectOrUnmap(aspace_, region_base, region_len, mmu_flags);
        if (result == ZX_OK) {
          return ZX_ERR_NEXT;
        }
        return result;
      });
  ASSERT(status == ZX_OK);
}

void VmMapping::AspaceDebugUnpinLocked(uint64_t offset, uint64_t len) const {
  LTRACEF("region %p obj_offset %#" PRIx64 " size %zu, offset %#" PRIx64 " len %#" PRIx64 "\n",
          this, object_offset_, size_, offset, len);

  canary_.Assert();

  // NOTE: must be acquired with the vmo lock held, but doesn't need to take
  // the address space lock, since it will not manipulate its location in the
  // vmar tree. However, it must be held in the ALIVE state across this call.
  //
  // Avoids a race with DestroyLocked() since it removes ourself from the VMO's
  // mapping list with the VMO lock held before dropping this state to DEAD. The
  // VMO cant call back to us once we're out of their list.
  DEBUG_ASSERT(get_state_locked_object() == LifeCycleState::ALIVE);

  // See if there's an intersect.
  vaddr_t base;
  uint64_t new_len;
  if (!ObjectRangeToVaddrRange(offset, len, &base, &new_len)) {
    return;
  }

  // This unpin is not allowed for kernel mappings, unless the mapping has specifically opted out of
  // this debug check due to it performing its own dynamic management.
  DEBUG_ASSERT(aspace_->is_user() || aspace_->is_guest_physical() ||
               flags_ & VMAR_FLAG_DEBUG_DYNAMIC_KERNEL_MAPPING);
}

namespace {

class VmMappingCoalescer {
 public:
  VmMappingCoalescer(VmMapping* mapping, vaddr_t base, uint mmu_flags,
                     ArchVmAspace::ExistingEntryAction existing_entry_action)
      TA_REQ(mapping->lock());
  ~VmMappingCoalescer();

  // Add a page to the mapping run.  If this fails, the VmMappingCoalescer is
  // no longer valid.
  zx_status_t Append(vaddr_t vaddr, paddr_t paddr) {
    AssertHeld(mapping_->lock_ref());
    DEBUG_ASSERT(!aborted_);
    // If this isn't the expected vaddr, flush the run we have first.
    if (count_ >= ktl::size(phys_) || vaddr != base_ + count_ * PAGE_SIZE) {
      zx_status_t status = Flush();
      if (status != ZX_OK) {
        return status;
      }
      base_ = vaddr;
    }
    phys_[count_] = paddr;
    ++count_;
    return ZX_OK;
  }

  // Submit any outstanding mappings to the MMU.  If this fails, the
  // VmMappingCoalescer is no longer valid.
  zx_status_t Flush();

  // Drop the current outstanding mappings without sending them to the MMU.
  // After this call, the VmMappingCoalescer is no longer valid.
  void Abort() { aborted_ = true; }

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(VmMappingCoalescer);

  VmMapping* mapping_;
  vaddr_t base_;
  paddr_t phys_[16];
  size_t count_;
  bool aborted_;
  const uint mmu_flags_;
  const ArchVmAspace::ExistingEntryAction existing_entry_action_;
};

VmMappingCoalescer::VmMappingCoalescer(VmMapping* mapping, vaddr_t base, uint mmu_flags,
                                       ArchVmAspace::ExistingEntryAction existing_entry_action)
    : mapping_(mapping),
      base_(base),
      count_(0),
      aborted_(false),
      mmu_flags_(mmu_flags),
      existing_entry_action_(existing_entry_action) {}

VmMappingCoalescer::~VmMappingCoalescer() {
  // Make sure we've flushed or aborted
  DEBUG_ASSERT(count_ == 0 || aborted_);
}

zx_status_t VmMappingCoalescer::Flush() {
  AssertHeld(mapping_->lock_ref());

  if (count_ == 0) {
    return ZX_OK;
  }

  if (mmu_flags_ & ARCH_MMU_FLAG_PERM_RWX_MASK) {
    size_t mapped;
    zx_status_t ret = mapping_->aspace()->arch_aspace().Map(base_, phys_, count_, mmu_flags_,
                                                            existing_entry_action_, &mapped);
    if (ret != ZX_OK) {
      TRACEF("error %d mapping %zu pages starting at va %#" PRIxPTR "\n", ret, count_, base_);
      aborted_ = true;
      return ret;
    }
    DEBUG_ASSERT_MSG(mapped == count_, "mapped %zu, count %zu\n", mapped, count_);
  }
  base_ += count_ * PAGE_SIZE;
  count_ = 0;
  return ZX_OK;
}

}  // namespace

zx_status_t VmMapping::MapRange(size_t offset, size_t len, bool commit, bool ignore_existing) {
  Guard<CriticalMutex> aspace_guard{aspace_->lock()};
  canary_.Assert();

  len = ROUNDUP(len, PAGE_SIZE);
  if (len == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }

  LTRACEF("region %p, offset %#zx, size %#zx, commit %d\n", this, offset, len, commit);

  DEBUG_ASSERT(object_);
  if (!IS_PAGE_ALIGNED(offset) || !is_in_range(base_ + offset, len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // If this is a kernel mapping then validate that all pages being mapped are currently pinned,
  // ensuring that they cannot be taken away for any reason, unless the mapping has specifically
  // opted out of this debug check due to it performing its own dynamic management.
  DEBUG_ASSERT(aspace_->is_user() || aspace_->is_guest_physical() ||
               (flags_ & VMAR_FLAG_DEBUG_DYNAMIC_KERNEL_MAPPING) ||
               object_->DebugIsRangePinned(object_offset_locked() + offset, len));

  // grab the lock for the vmo
  Guard<CriticalMutex> object_guard{object_->lock()};

  // Cache whether the object is dirty tracked, we need to know this when computing mmu flags later.
  const bool dirty_tracked = object_->is_dirty_tracked_locked();

  // set the currently faulting flag for any recursive calls the vmo may make back into us.
  DEBUG_ASSERT(!currently_faulting_);
  currently_faulting_ = true;
  auto cleanup = fit::defer([&]() {
    assert_object_lock();
    currently_faulting_ = false;
  });

  // The region to map could have multiple different current arch mmu flags, so we need to iterate
  // over them to ensure we install mappings with the correct permissions.
  return EnumerateProtectionRangesLocked(
      base_ + offset, len,
      [this, commit, dirty_tracked, ignore_existing](vaddr_t base, size_t len, uint mmu_flags) {
        AssertHeld(aspace_->lock_ref());
        AssertHeld(object_->lock_ref());
        // Remove the write permission if this maps a vmo that supports dirty tracking, in order to
        // trigger write permission faults when writes occur, enabling us to track when pages are
        // dirtied.
        if (dirty_tracked) {
          mmu_flags &= ~ARCH_MMU_FLAG_PERM_WRITE;
        }
        // precompute the flags we'll pass LookupPagesLocked
        uint pf_flags = (mmu_flags & ARCH_MMU_FLAG_PERM_WRITE) ? VMM_PF_FLAG_WRITE : 0;
        // if committing, then tell it to soft fault in a page
        if (commit) {
          pf_flags |= VMM_PF_FLAG_SW_FAULT;
        }

        // In the scenario where we are committing, and are setting the SW_FAULT flag, we are
        // supposed to pass in a non-null LazyPageRequest to LookupPagesLocked. Technically we could
        // get away with not passing in a PageRequest since:
        //  * Only internal kernel VMOs will have the 'commit' flag passed in for their mappings
        //  * Only pager backed VMOs need to fill out a PageRequest
        //  * Internal kernel VMOs are never pager backed.
        // However, should these assumptions ever get violated it's better to catch this gracefully
        // than have LookupPagesLocked error/crash internally, and it costs nothing to create and
        // pass in.
        __UNINITIALIZED LazyPageRequest page_request;

        // iterate through the range, grabbing a page from the underlying object and
        // mapping it in
        VmMappingCoalescer coalescer(this, base, mmu_flags,
                                     ignore_existing ? ArchVmAspace::ExistingEntryAction::Skip
                                                     : ArchVmAspace::ExistingEntryAction::Error);
        __UNINITIALIZED VmObject::LookupInfo pages;
        for (size_t offset = 0; offset < len;) {
          const uint64_t vmo_offset = object_offset_ + (base - base_) + offset;

          zx_status_t status;
          status = object_->LookupPagesLocked(
              vmo_offset, pf_flags, VmObject::DirtyTrackingAction::None,
              ktl::min((len - offset) / PAGE_SIZE, VmObject::LookupInfo::kMaxPages), nullptr,
              &page_request, &pages);
          if (status != ZX_OK) {
            // As per the comment above page_request definition, there should never be SW_FAULT +
            // pager backed VMO and so we should never end up with a PageRequest needing to be
            // waited on.
            ASSERT(status != ZX_ERR_SHOULD_WAIT);
            // no page to map
            if (commit) {
              // fail when we can't commit every requested page
              coalescer.Abort();
              return status;
            }

            // skip ahead
            offset += PAGE_SIZE;
            continue;
          }
          DEBUG_ASSERT(pages.num_pages > 0);

          vaddr_t va = base + offset;
          for (uint32_t i = 0; i < pages.num_pages; i++, va += PAGE_SIZE, offset += PAGE_SIZE) {
            LTRACEF_LEVEL(2, "mapping pa %#" PRIxPTR " to va %#" PRIxPTR "\n", pages.paddrs[i], va);
            status = coalescer.Append(va, pages.paddrs[i]);
            if (status != ZX_OK) {
              return status;
            }
          }
        }
        return coalescer.Flush();
      });
}

zx_status_t VmMapping::DecommitRange(size_t offset, size_t len) {
  canary_.Assert();
  LTRACEF("%p [%#zx+%#zx], offset %#zx, len %#zx\n", this, base_, size_, offset, len);

  Guard<CriticalMutex> guard{aspace_->lock()};
  if (state_ != LifeCycleState::ALIVE) {
    return ZX_ERR_BAD_STATE;
  }
  if (offset + len < offset || offset + len > size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  // VmObject::DecommitRange will typically call back into our instance's
  // VmMapping::AspaceUnmapVmoRangeLocked.
  return object_->DecommitRange(object_offset_locked() + offset, len);
}

zx_status_t VmMapping::DestroyLocked() {
  canary_.Assert();
  LTRACEF("%p\n", this);

  // Take a reference to ourself, so that we do not get destructed after
  // dropping our last reference in this method (e.g. when calling
  // subregions_.erase below).
  fbl::RefPtr<VmMapping> self(this);

  // If this is the last_fault_ then clear it before removing from the VMAR tree. Even if this
  // destroy fails, it's always safe to clear last_fault_, so we preference doing it upfront for
  // clarity.
  if (aspace_->last_fault_ == this) {
    aspace_->last_fault_ = nullptr;
  }

  // The vDSO code mapping can never be unmapped, not even
  // by VMAR destruction (except for process exit, of course).
  // TODO(mcgrathr): Turn this into a policy-driven process-fatal case
  // at some point.  teisenbe@ wants to eventually make zx_vmar_destroy
  // never fail.
  if (aspace_->vdso_code_mapping_ == self) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // grab the object lock to unmap and remove ourselves from its list.
  {
    Guard<CriticalMutex> guard{object_->lock()};
    // The unmap needs to be performed whilst the object lock is being held so that set_size_locked
    // can be called without there being an opportunity for mappings to be modified in between.
    zx_status_t status = aspace_->arch_aspace().Unmap(base_, size_ / PAGE_SIZE,
                                                      aspace_->EnlargeArchUnmap(), nullptr);
    if (status != ZX_OK) {
      return status;
    }
    protection_ranges_.clear();
    set_size_locked(0);
    object_->RemoveMappingLocked(this);
  }

  // Clear the cached attribution count.
  // The generation count should already have been incremented by UnmapLocked above.
  cached_page_attribution_ = {};

  // detach from any object we have mapped. Note that we are holding the aspace_->lock() so we
  // will not race with other threads calling vmo()
  object_.reset();

  // Detach the now dead region from the parent
  if (parent_) {
    AssertHeld(parent_->lock_ref());
    DEBUG_ASSERT(this->in_subregion_tree());
    parent_->subregions_.RemoveRegion(this);
  }

  // mark ourself as dead
  parent_ = nullptr;
  state_ = LifeCycleState::DEAD;
  return ZX_OK;
}

zx_status_t VmMapping::PageFault(vaddr_t va, const uint pf_flags, LazyPageRequest* page_request) {
  VM_KTRACE_DURATION(2, "VmMapping::PageFault", va, pf_flags);
  canary_.Assert();

  DEBUG_ASSERT(is_in_range(va, 1));

  va = ROUNDDOWN(va, PAGE_SIZE);
  uint64_t vmo_offset = va - base_ + object_offset_locked();

  __UNUSED char pf_string[5];
  LTRACEF("%p va %#" PRIxPTR " vmo_offset %#" PRIx64 ", pf_flags %#x (%s)\n", this, va, vmo_offset,
          pf_flags, vmm_pf_flags_to_string(pf_flags, pf_string));

  // Need to look up the mmu flags for this virtual address, as well as how large a region those
  // flags are for so we can cap the extra mappings we create.
  MappingProtectionRanges::FlagsRange range =
      ProtectRangesLocked().FlagsRangeAtAddr(base_, size_, va);

  // Build the mmu flags we need to have based on the page fault. This strategy of building the
  // flags and then comparing all at once allows the compiler to provide much better code gen.
  uint needed_mmu_flags = 0;
  if (pf_flags & VMM_PF_FLAG_USER) {
    needed_mmu_flags |= ARCH_MMU_FLAG_PERM_USER;
  }
  if (pf_flags & VMM_PF_FLAG_WRITE) {
    needed_mmu_flags |= ARCH_MMU_FLAG_PERM_WRITE;
  } else {
    needed_mmu_flags |= ARCH_MMU_FLAG_PERM_READ;
  }
  if (pf_flags & VMM_PF_FLAG_INSTRUCTION) {
    needed_mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
  }
  // Check that all the needed flags are present.
  if (unlikely((range.mmu_flags & needed_mmu_flags) != needed_mmu_flags)) {
    if ((pf_flags & VMM_PF_FLAG_USER) && !(range.mmu_flags & ARCH_MMU_FLAG_PERM_USER)) {
      // user page fault on non user mapped region
      LTRACEF("permission failure: user fault on non user region\n");
    }
    if ((pf_flags & VMM_PF_FLAG_WRITE) && !(range.mmu_flags & ARCH_MMU_FLAG_PERM_WRITE)) {
      // write to a non-writeable region
      LTRACEF("permission failure: write fault on non-writable region\n");
    }
    if (!(pf_flags & VMM_PF_FLAG_WRITE) && !(range.mmu_flags & ARCH_MMU_FLAG_PERM_READ)) {
      // read to a non-readable region
      LTRACEF("permission failure: read fault on non-readable region\n");
    }
    if ((pf_flags & VMM_PF_FLAG_INSTRUCTION) && !(range.mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE)) {
      // instruction fetch from a no execute region
      LTRACEF("permission failure: execute fault on no execute region\n");
    }
    return ZX_ERR_ACCESS_DENIED;
  }

  // grab the lock for the vmo
  Guard<CriticalMutex> guard{object_->lock()};

  // Determine how far to the end of the page table so we do not cause extra allocations.
  const uint64_t next_pt_base = ArchVmAspace::NextUserPageTableOffset(va);
  // Find the minimum between the size of this protection range and the end of the page table.
  const uint64_t max_map = ktl::min(next_pt_base, range.region_top);
  // Convert this into a number of pages, limited by the max lookup window.
  //
  // If this is a write fault and the VMO supports dirty tracking, only lookup 1 page. The pages
  // will also be marked dirty for a write, which we only want for the current page. We could
  // optimize this to lookup following pages here too and map them in, however we would have to not
  // mark them dirty in LookupPagesLocked, and map them in without write permissions here, so that
  // we can take a permission fault on a write to update their dirty tracking later. Instead, we can
  // keep things simple by just looking up 1 page.
  // TODO(rashaeqbal): Revisit this decision if there are performance issues.
  const uint64_t max_pages =
      (pf_flags & VMM_PF_FLAG_WRITE && object_->is_dirty_tracked_locked())
          ? 1
          : ktl::min((max_map - va) / PAGE_SIZE, VmObject::LookupInfo::kMaxPages);
  DEBUG_ASSERT(max_pages > 0);

  // set the currently faulting flag for any recursive calls the vmo may make back into us
  // The specific path we're avoiding is if the VMO calls back into us during
  // vmo->LookupPagesLocked() via AspaceUnmapVmoRangeLocked(). Since we're responsible for that
  // page, signal to ourself to skip the unmap operation.
  DEBUG_ASSERT(!currently_faulting_);
  currently_faulting_ = true;
  auto cleanup = fit::defer([&]() {
    assert_object_lock();
    currently_faulting_ = false;
  });

  // fault in or grab existing pages.
  __UNINITIALIZED VmObject::LookupInfo lookup_info;
  zx_status_t status = object_->LookupPagesLocked(
      vmo_offset, pf_flags, VmObject::DirtyTrackingAction::DirtyAllPagesOnWrite, max_pages, nullptr,
      page_request, &lookup_info);
  if (status != ZX_OK) {
    // TODO(cpu): This trace was originally TRACEF() always on, but it fires if the
    // VMO was resized, rather than just when the system is running out of memory.
    LTRACEF("ERROR: failed to fault in or grab existing page: %d\n", (int)status);
    LTRACEF("%p vmo_offset %#" PRIx64 ", pf_flags %#x\n", this, vmo_offset, pf_flags);
    // TODO(rashaeqbal): Audit error codes from LookupPagesLocked and make sure we're not returning
    // something unexpected. Document expected return codes and constrain them if required.
    return status;
  }
  DEBUG_ASSERT(lookup_info.num_pages > 0);

  // We looked up in order to write. Mark as modified.
  if (pf_flags & VMM_PF_FLAG_WRITE) {
    object_->mark_modified_locked();
  }

  // if we read faulted, and lookup didn't say that this is always writable, then we map or modify
  // the page without any write permissions. This ensures we will fault again if a write is
  // attempted so we can potentially replace this page with a copy or a new one, or update the
  // page's dirty state.
  if (!(pf_flags & VMM_PF_FLAG_WRITE) && !lookup_info.writable) {
    // we read faulted, so only map with read permissions
    range.mmu_flags &= ~ARCH_MMU_FLAG_PERM_WRITE;
  }

  // If we are faulting a page into a guest, clean the caches.
  //
  // Cleaning pages is required to ensure that guests --- who can disable
  // their own caches at will --- don't get access to stale (potentially
  // sensitive) data in memory that hasn't been written back yet.
  if ((pf_flags & VMM_PF_FLAG_GUEST) != 0) {
    ArchVmICacheConsistencyManager sync_cm;
    for (size_t i = 0; i < lookup_info.num_pages; i++) {
      // Ignore non-physmap pages, such as passed through device ranges.
      if (unlikely(!is_physmap_phys_addr(lookup_info.paddrs[i]))) {
        continue;
      }

      vaddr_t vaddr = reinterpret_cast<vaddr_t>(paddr_to_physmap(lookup_info.paddrs[i]));
      arch_clean_cache_range(vaddr, PAGE_SIZE);  // Clean d-cache
      sync_cm.SyncAddr(vaddr, PAGE_SIZE);        // Sync i-cache with d-cache.
    }
  }

  VM_KTRACE_DURATION_BEGIN(2, "map_page", va, pf_flags);
  auto trace_end =
      fit::defer([va, pf_flags]() { VM_KTRACE_DURATION_END(2, "map_page", va, pf_flags); });

  // see if something is mapped here now
  // this may happen if we are one of multiple threads racing on a single address
  uint page_flags;
  paddr_t pa;
  zx_status_t err = aspace_->arch_aspace().Query(va, &pa, &page_flags);
  if (err >= 0) {
    LTRACEF("queried va, page at pa %#" PRIxPTR ", flags %#x is already there\n", pa, page_flags);
    if (pa == lookup_info.paddrs[0]) {
      // Faulting on a mapping that is the correct page could happen for a few reasons
      //  1. Permission are incorrect and this fault is a write fault for a read only mapping.
      //  2. Fault was caused by (1), but we were racing with another fault and the mapping is
      //     already fixed.
      //  3. Some other error, such as an access flag missing on arm, caused this fault
      // Of these three scenarios (1) is overwhelmingly the most common, and requires us to protect
      // the page with the new permissions. In the scenario of (2) we could fast return and not
      // perform the potentially expensive protect, but this scenario is quite rare and requires a
      // multi-thread race on causing and handling the fault. (3) should also be highly uncommon as
      // access faults would normally be handled by a separate fault handler, nevertheless we should
      // still resolve such faults here, which requires calling protect.
      // Given that (2) is rare and hard to distinguish from (3) we simply always call protect to
      // ensure the fault is resolved.

      // assert that we're not accidentally marking the zero page writable
      DEBUG_ASSERT((pa != vm_get_zero_page_paddr()) ||
                   !(range.mmu_flags & ARCH_MMU_FLAG_PERM_WRITE));

      // same page, different permission
      status = aspace_->arch_aspace().Protect(va, 1, range.mmu_flags);
      if (unlikely(status != ZX_OK)) {
        // ZX_ERR_NO_MEMORY is the only legitimate reason for Protect to fail.
        ASSERT_MSG(status == ZX_ERR_NO_MEMORY, "Unexpected failure from protect: %d\n", status);

        TRACEF("failed to modify permissions on existing mapping\n");
        return status;
      }
    } else {
      // some other page is mapped there already
      LTRACEF("thread %s faulted on va %#" PRIxPTR ", different page was present\n",
              Thread::Current::Get()->name(), va);

      // assert that we're not accidentally mapping the zero page writable
      DEBUG_ASSERT(!(range.mmu_flags & ARCH_MMU_FLAG_PERM_WRITE) ||
                   ktl::all_of(lookup_info.paddrs, &lookup_info.paddrs[lookup_info.num_pages],
                               [](paddr_t p) { return p != vm_get_zero_page_paddr(); }));

      // unmap the old one and put the new one in place
      status = aspace_->arch_aspace().Unmap(va, 1, aspace_->EnlargeArchUnmap(), nullptr);
      if (status != ZX_OK) {
        ASSERT_MSG(status == ZX_ERR_NO_MEMORY, "Unexpected failure from unmap: %d\n", status);
        TRACEF("failed to remove old mapping before replacing\n");
        return status;
      }

      size_t mapped;
      status =
          aspace_->arch_aspace().Map(va, lookup_info.paddrs, lookup_info.num_pages, range.mmu_flags,
                                     ArchVmAspace::ExistingEntryAction::Skip, &mapped);
      if (status != ZX_OK) {
        ASSERT_MSG(status == ZX_ERR_NO_MEMORY, "Unexpected failure from map: %d\n", status);
        TRACEF("failed to map replacement page\n");
        return status;
      }
      DEBUG_ASSERT(mapped >= 1);

      return ZX_OK;
    }
  } else {
    // nothing was mapped there before, map it now

    // assert that we're not accidentally mapping the zero page writable
    DEBUG_ASSERT(!(range.mmu_flags & ARCH_MMU_FLAG_PERM_WRITE) ||
                 ktl::all_of(lookup_info.paddrs, &lookup_info.paddrs[lookup_info.num_pages],
                             [](paddr_t p) { return p != vm_get_zero_page_paddr(); }));

    size_t mapped;
    status =
        aspace_->arch_aspace().Map(va, lookup_info.paddrs, lookup_info.num_pages, range.mmu_flags,
                                   ArchVmAspace::ExistingEntryAction::Skip, &mapped);
    if (status != ZX_OK) {
      ASSERT_MSG(status == ZX_ERR_NO_MEMORY, "Unexpected failure from map: %d\n", status);
      TRACEF("failed to map page %d\n", status);
      return status;
    }
    DEBUG_ASSERT(mapped >= 1);
  }

  return ZX_OK;
}

void VmMapping::ActivateLocked() {
  DEBUG_ASSERT(state_ == LifeCycleState::NOT_READY);
  DEBUG_ASSERT(parent_);

  state_ = LifeCycleState::ALIVE;
  object_->AddMappingLocked(this);
  AssertHeld(parent_->lock_ref());
  parent_->subregions_.InsertRegion(fbl::RefPtr<VmAddressRegionOrMapping>(this));
}

void VmMapping::Activate() {
  Guard<CriticalMutex> guard{object_->lock()};
  ActivateLocked();
}

void VmMapping::TryMergeRightNeighborLocked(VmMapping* right_candidate) {
  AssertHeld(right_candidate->lock_ref());

  // This code is tolerant of many 'miss calls' if mappings aren't mergeable or are not neighbours
  // etc, but the caller should not be attempting to merge if these mappings are not actually from
  // the same vmar parent. Doing so indicates something structurally wrong with the hierarchy.
  DEBUG_ASSERT(parent_ == right_candidate->parent_);

  // These tests are intended to be ordered such that we fail as fast as possible. As such testing
  // for mergeability, which we commonly expect to succeed and not fail, is done last.

  // Need to refer to the same object.
  if (object_.get() != right_candidate->object_.get()) {
    return;
  }
  // Aspace and VMO ranges need to be contiguous. Validate that the right candidate is actually to
  // the right in addition to checking that base+size lines up for single scenario where base_+size_
  // can overflow and becomes zero.
  if (base_ + size_ != right_candidate->base_ || right_candidate->base_ < base_) {
    return;
  }
  if (object_offset_locked() + size_ != right_candidate->object_offset_locked()) {
    return;
  }
  // All flags need to be consistent.
  if (flags_ != right_candidate->flags_) {
    return;
  }
  // Although we can combine the protect_region_list_rest_ of the two mappings, we require that they
  // be of the same cacheability, as this is an assumption that mapping has a single cacheability
  // type. Since all protection regions have the same cacheability we can check any arbitrary one in
  // each of the mappings. Note that this check is technically redundant, since a VMO can only have
  // one kind of cacheability and we already know this is the same VMO, but some extra paranoia here
  // does not hurt.
  if ((ProtectRangesLocked().FirstRegionMmuFlags() & ARCH_MMU_FLAG_CACHE_MASK) !=
      (right_candidate->ProtectRangesLocked().FirstRegionMmuFlags() & ARCH_MMU_FLAG_CACHE_MASK)) {
    return;
  }

  // Only merge live mappings.
  if (state_ != LifeCycleState::ALIVE || right_candidate->state_ != LifeCycleState::ALIVE) {
    return;
  }
  // Both need to be mergeable.
  if (mergeable_ == Mergeable::NO || right_candidate->mergeable_ == Mergeable::NO) {
    return;
  }

  // Destroy / DestroyLocked perform a lot more cleanup than we want, we just need to clear out a
  // few things from right_candidate and then mark it as dead, as we do not want to clear out any
  // arch page table mappings etc.
  {
    // Although it was safe to read size_ without holding the object lock, we need to acquire it to
    // perform changes.
    Guard<CriticalMutex> guard{right_candidate->object_->lock()};
    AssertHeld(object_->lock_ref());

    // Attempt to merge the protection region lists first. This is done first as a node allocation
    // might be needed, which could fail. If it fails we can still abort now without needing to roll
    // back any changes.
    zx_status_t status = protection_ranges_.MergeRightNeighbor(right_candidate->protection_ranges_,
                                                               right_candidate->base_);
    if (status != ZX_OK) {
      ASSERT(status == ZX_ERR_NO_MEMORY);
      return;
    }

    set_size_locked(size_ + right_candidate->size_);
    right_candidate->set_size_locked(0);

    right_candidate->object_->RemoveMappingLocked(right_candidate);
  }

  // Detach from the VMO.
  right_candidate->object_.reset();

  // Detach the now dead region from the parent, ensuring our caller is correctly holding a refptr.
  DEBUG_ASSERT(right_candidate->in_subregion_tree());
  DEBUG_ASSERT(right_candidate->ref_count_debug() > 1);
  AssertHeld(parent_->lock_ref());
  parent_->subregions_.RemoveRegion(right_candidate);
  if (aspace_->last_fault_ == right_candidate) {
    aspace_->last_fault_ = nullptr;
  }

  // Mark it as dead.
  right_candidate->parent_ = nullptr;
  right_candidate->state_ = LifeCycleState::DEAD;

  vm_mappings_merged.Add(1);
}

void VmMapping::TryMergeNeighborsLocked() {
  canary_.Assert();

  // Check that this mapping is mergeable and is currently in the correct lifecycle state.
  if (mergeable_ == Mergeable::NO || state_ != LifeCycleState::ALIVE) {
    return;
  }
  // As a VmMapping if we we are alive we by definition have a parent.
  DEBUG_ASSERT(parent_);

  // We expect there to be a RefPtr to us held beyond the one for the wavl tree ensuring that we
  // cannot trigger our own destructor should we remove ourselves from the hierarchy.
  DEBUG_ASSERT(ref_count_debug() > 1);

  // First consider merging any mapping on our right, into |this|.
  AssertHeld(parent_->lock_ref());
  auto right_candidate = parent_->subregions_.RightOf(this);
  if (right_candidate.IsValid()) {
    // Request mapping as a refptr as we need to hold a refptr across the try merge.
    if (fbl::RefPtr<VmMapping> mapping = right_candidate->as_vm_mapping()) {
      TryMergeRightNeighborLocked(mapping.get());
    }
  }

  // Now attempt to merge |this| with any left neighbor.
  AssertHeld(parent_->lock_ref());
  auto left_candidate = parent_->subregions_.LeftOf(this);
  if (!left_candidate.IsValid()) {
    return;
  }
  if (auto mapping = left_candidate->as_vm_mapping()) {
    // Attempt actual merge. If this succeeds then |this| is in the dead state, but that's fine as
    // we are finished anyway.
    AssertHeld(mapping->lock_ref());
    mapping->TryMergeRightNeighborLocked(this);
  }
}

void VmMapping::MarkMergeable(fbl::RefPtr<VmMapping>&& mapping) {
  Guard<CriticalMutex> guard{mapping->lock()};
  // Now that we have the lock check this mapping is still alive and we haven't raced with some
  // kind of destruction.
  if (mapping->state_ != LifeCycleState::ALIVE) {
    return;
  }
  // Skip marking any vdso segments mergeable. Although there is currently only one vdso segment and
  // so it would never actually get merged, marking it mergeable is technically incorrect.
  if (mapping->aspace_->vdso_code_mapping_ == mapping) {
    return;
  }
  mapping->mergeable_ = Mergeable::YES;
  mapping->TryMergeNeighborsLocked();
}

zx_status_t MappingProtectionRanges::EnumerateProtectionRanges(
    vaddr_t mapping_base, size_t mapping_size, vaddr_t base, size_t size,
    fit::inline_function<zx_status_t(vaddr_t region_base, size_t region_len, uint mmu_flags)>&&
        func) const {
  DEBUG_ASSERT(size > 0);

  // Have a short circuit for the single protect region case to avoid wavl tree processing in the
  // common case.
  if (protect_region_list_rest_.is_empty()) {
    zx_status_t result = func(base, size, first_region_arch_mmu_flags_);
    if (result == ZX_ERR_NEXT || result == ZX_ERR_STOP) {
      return ZX_OK;
    }
    return result;
  }

  // See comments in the loop that explain what next and current represent.
  auto next = protect_region_list_rest_.upper_bound(base);
  auto current = next;
  current--;
  const vaddr_t range_top = base + (size - 1);
  do {
    // The region starting from 'current' and ending at 'next' represents a single protection
    // domain. We first work that, remembering that either of these could be an invalid node,
    // meaning the start or end of the mapping respectively.
    const vaddr_t protect_region_base = current.IsValid() ? current->region_start : mapping_base;
    const vaddr_t protect_region_top =
        next.IsValid() ? (next->region_start - 1) : (mapping_base + (mapping_size - 1));
    // We should only be iterating nodes that are actually part of the requested range.
    DEBUG_ASSERT(base <= protect_region_top);
    DEBUG_ASSERT(range_top >= protect_region_base);
    // The region found is of an entire protection block, and could extend outside the requested
    // range, so trim if necessary.
    const vaddr_t region_base = ktl::max(protect_region_base, base);
    const size_t region_len = ktl::min(protect_region_top, range_top) - region_base + 1;
    zx_status_t result =
        func(region_base, region_len,
             current.IsValid() ? current->arch_mmu_flags : first_region_arch_mmu_flags_);
    if (result != ZX_ERR_NEXT) {
      if (result == ZX_ERR_STOP) {
        return ZX_OK;
      }
      return result;
    }
    // Move to the next block.
    current = next;
    next++;
    // Continue looping as long we operating on nodes that overlap with the requested range.
  } while (current.IsValid() && current->region_start <= range_top);

  return ZX_OK;
}

template <typename F>
zx_status_t MappingProtectionRanges::UpdateProtectionRange(vaddr_t mapping_base,
                                                           size_t mapping_size, vaddr_t base,
                                                           size_t size, uint new_arch_mmu_flags,
                                                           F callback) {
  // If we're changing the whole mapping, just make the change.
  if (mapping_base == base && mapping_size == size) {
    protect_region_list_rest_.clear();
    callback(base, size, first_region_arch_mmu_flags_);
    first_region_arch_mmu_flags_ = new_arch_mmu_flags;
    return ZX_OK;
  }

  // Find the range of nodes that will need deleting.
  auto first = protect_region_list_rest_.lower_bound(base);
  auto last = protect_region_list_rest_.upper_bound(base + (size - 1));

  // Work the flags in the regions before the first/last nodes. We need to cache these flags so that
  // once we are inserting the new protection nodes, we do not insert nodes such that we would cause
  // two regions to have the same flags (which would be redundant).
  const uint start_carry_flags = FlagsForPreviousRegion(first);
  const uint end_carry_flags = FlagsForPreviousRegion(last);

  // Determine how many new nodes we are going to need so we can allocate up front. This ensures
  // that after we have deleted nodes from the tree (and destroyed information) we do not have to
  // do an allocation that might fail and leave us in an unrecoverable state. However, we would
  // like to avoid actually performing allocations as far as possible, so do the following
  // 1. Count how many nodes will be needed to represent the new protection range (after the nodes
  //    between first,last have been deleted. As a protection range has two points, a start and an
  //    end, the most nodes we can ever possibly need is two.
  // 2. Of these new nodes we will need, work out how many we can reuse from deletion.
  // 3. Allocate the remainder.
  ktl::optional<ktl::unique_ptr<ProtectNode>> protect_nodes[2];
  const uint total_nodes_needed = NodeAllocationsForRange(mapping_base, mapping_size, base, size,
                                                          first, last, new_arch_mmu_flags);
  uint nodes_needed = total_nodes_needed;
  // First see how many of the nodes we will be able to get by erasing and can reuse.
  for (auto it = first; nodes_needed > 0 && it != last; it++) {
    nodes_needed--;
  }
  // If there are any nodes_needed still, allocate them so that they are available.
  uint nodes_available = 0;
  // Allocate any remaining nodes_needed that we will not fulfill from deletions.
  while (nodes_available < nodes_needed) {
    fbl::AllocChecker ac;
    ktl::unique_ptr<ProtectNode> new_node(ktl::make_unique<ProtectNode>(&ac));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    protect_nodes[nodes_available++].emplace(ktl::move(new_node));
  }

  // Now that we have done all memory allocations and know that we cannot fail start the destructive
  // part and erase any nodes in the the range as well as call the provided callback with the old
  // data.
  {
    vaddr_t old_start = base;
    uint old_flags = start_carry_flags;
    while (first != last) {
      // On the first iteration if the range is aligned to a node then we skip, since we do not want
      // to do the callback for a zero sized range.
      if (old_start != first->region_start) {
        callback(old_start, first->region_start - old_start, old_flags);
      }
      old_start = first->region_start;
      old_flags = first->arch_mmu_flags;
      auto node = protect_region_list_rest_.erase(first++);
      if (nodes_available < total_nodes_needed) {
        protect_nodes[nodes_available++].emplace(ktl::move(node));
      }
    }
    // If the range was not aligned to a node then process any remainder.
    if (old_start <= base + (size - 1)) {
      callback(old_start, base + size - old_start, old_flags);
    }
  }

  // At this point we should now have all the nodes.
  DEBUG_ASSERT(total_nodes_needed == nodes_available);

  // Check if we are updating the implicit first node, which just involves changing
  // first_region_arch_mmu_flags_, or if there's a protection change that requires a node insertion.
  if (base == mapping_base) {
    first_region_arch_mmu_flags_ = new_arch_mmu_flags;
  } else if (start_carry_flags != new_arch_mmu_flags) {
    ASSERT(nodes_available > 0);
    auto node = ktl::move(protect_nodes[--nodes_available].value());
    node->region_start = base;
    node->arch_mmu_flags = new_arch_mmu_flags;
    protect_region_list_rest_.insert(ktl::move(node));
  }

  // To create the end of the region we first check if there is a gap between the end of this region
  // and the start of the next region. Additionally this needs to handle the case where there is no
  // next node in the tree, and so we have to check against mapping limit of mapping_base +
  // mapping_size.
  const uint64_t next_region_start =
      last.IsValid() ? last->region_start : (mapping_base + mapping_size);
  if (next_region_start != base + size) {
    // There is a gap to the next node so we need to make sure it keeps its old protection value,
    // end_carry_flags. However, it could have ended up that these flags are what we are protecting
    // to, in which case a new node isn't needed as we can just effectively merge the gap into this
    // protection range.
    if (end_carry_flags != new_arch_mmu_flags) {
      ASSERT(nodes_available > 0);
      auto node = ktl::move(protect_nodes[--nodes_available].value());
      node->region_start = base + size;
      node->arch_mmu_flags = end_carry_flags;
      protect_region_list_rest_.insert(ktl::move(node));
      // Since we are essentially moving forward a node that we previously deleted, to essentially
      // shrink the previous protection range, we know that there is no merging needed with the next
      // node.
      DEBUG_ASSERT(!last.IsValid() || last->arch_mmu_flags != end_carry_flags);
    }
  } else if (last.IsValid() && last->arch_mmu_flags == new_arch_mmu_flags) {
    // From the previous `if` block we know that if last.IsValid is true, then the end of the region
    // being protected is last->region_start. If this next region happens to have the same flags as
    // what we just protected, then we need to drop this node.
    protect_region_list_rest_.erase(last);
  }

  // We should not have allocated more nodes than we needed, this indicates a bug in the calculation
  // logic.
  DEBUG_ASSERT(nodes_available == 0);
  return ZX_OK;
}

uint MappingProtectionRanges::MmuFlagsForWavlRegion(vaddr_t vaddr) const {
  DEBUG_ASSERT(!protect_region_list_rest_.is_empty());
  auto it = --protect_region_list_rest_.upper_bound(vaddr);
  if (it.IsValid()) {
    DEBUG_ASSERT(it->region_start <= vaddr);
    return it->arch_mmu_flags;
  } else {
    DEBUG_ASSERT(protect_region_list_rest_.begin()->region_start > vaddr);
    return first_region_arch_mmu_flags_;
  }
}

// Counts how many nodes would need to be allocated for a protection range. This calculation is
// based of whether there are actually changes in the protection type that require a node to be
// added.
uint MappingProtectionRanges::NodeAllocationsForRange(vaddr_t mapping_base, size_t mapping_size,
                                                      vaddr_t base, size_t size,
                                                      RegionList::iterator removal_start,
                                                      RegionList::iterator removal_end,
                                                      uint new_mmu_flags) const {
  uint nodes_needed = 0;
  // Check if we will need a node at the start. if base==base_ then we will just be changing the
  // first_region_arch_mmu_flags_, otherwise we need a node if we're actually causing a protection
  // change.
  if (base != mapping_base && FlagsForPreviousRegion(removal_start) != new_mmu_flags) {
    nodes_needed++;
  }
  // The node for the end of the region is needed under two conditions
  // 1. There will be a non-zero gap between the end of our new region and the start of the next
  //    existing region.
  // 2. This non-zero sized gap is of a different protection type.
  const uint64_t next_region_start =
      removal_end.IsValid() ? removal_end->region_start : (mapping_base + mapping_size);
  if (next_region_start != base + size && FlagsForPreviousRegion(removal_end) != new_mmu_flags) {
    nodes_needed++;
  }
  return nodes_needed;
}

zx_status_t MappingProtectionRanges::MergeRightNeighbor(MappingProtectionRanges& right,
                                                        vaddr_t merge_addr) {
  // We need to insert a node if the protection type of the end of the left mapping is not the
  // same as the protection type of the start of the right mapping.
  if (FlagsForPreviousRegion(protect_region_list_rest_.end()) !=
      right.first_region_arch_mmu_flags_) {
    fbl::AllocChecker ac;
    ktl::unique_ptr<ProtectNode> region =
        ktl::make_unique<ProtectNode>(&ac, merge_addr, right.first_region_arch_mmu_flags_);
    if (!ac.check()) {
      // No state has changed yet, so even though we do not forward up an error it is safe to just
      // not merge.
      TRACEF("Aborted region merge due to out of memory\n");
      return ZX_ERR_NO_MEMORY;
    }
    protect_region_list_rest_.insert(ktl::move(region));
  }
  // Carry over any remaining regions.
  while (!right.protect_region_list_rest_.is_empty()) {
    protect_region_list_rest_.insert(right.protect_region_list_rest_.pop_front());
  }
  return ZX_OK;
}

MappingProtectionRanges MappingProtectionRanges::SplitAt(vaddr_t split) {
  // Determine the mmu flags the right most mapping would start at.
  auto right_nodes = protect_region_list_rest_.upper_bound(split);
  const uint right_mmu_flags = FlagsForPreviousRegion(right_nodes);

  MappingProtectionRanges ranges(right_mmu_flags);

  // Move any protect regions into the right half.
  while (right_nodes != protect_region_list_rest_.end()) {
    ranges.protect_region_list_rest_.insert(protect_region_list_rest_.erase(right_nodes++));
  }
  return ranges;
}

void MappingProtectionRanges::DiscardBelow(vaddr_t addr) {
  auto last = protect_region_list_rest_.upper_bound(addr);
  while (protect_region_list_rest_.begin() != last) {
    first_region_arch_mmu_flags_ = protect_region_list_rest_.pop_front()->arch_mmu_flags;
  }
}

void MappingProtectionRanges::DiscardAbove(vaddr_t addr) {
  for (auto it = protect_region_list_rest_.lower_bound(addr);
       it != protect_region_list_rest_.end();) {
    protect_region_list_rest_.erase(it++);
  }
}

bool MappingProtectionRanges::DebugNodesWithinRange(vaddr_t mapping_base, size_t mapping_size) {
  if (protect_region_list_rest_.is_empty()) {
    return true;
  }
  if (protect_region_list_rest_.begin()->region_start < mapping_base) {
    return false;
  }
  if ((--protect_region_list_rest_.end())->region_start >= mapping_base + mapping_size) {
    return false;
  }
  return true;
}
