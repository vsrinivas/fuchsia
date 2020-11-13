// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_VM_ADDRESS_REGION_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_VM_ADDRESS_REGION_H_

#include <assert.h>
#include <lib/crypto/prng.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdint.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <ktl/limits.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_page_list.h>

// Creation flags for VmAddressRegion and VmMappings

// When randomly allocating subregions, reduce sprawl by placing allocations
// near each other.
#define VMAR_FLAG_COMPACT (1 << 0)
// Request that the new region be at the specified offset in its parent region.
#define VMAR_FLAG_SPECIFIC (1 << 1)
// Like VMAR_FLAG_SPECIFIC, but permits overwriting existing mappings.  This
// flag will not overwrite through a subregion.
#define VMAR_FLAG_SPECIFIC_OVERWRITE (1 << 2)
// Allow VmMappings to be created inside the new region with the SPECIFIC or
// OFFSET_IS_UPPER_LIMIT flag.
#define VMAR_FLAG_CAN_MAP_SPECIFIC (1 << 3)
// When on a VmAddressRegion, allow VmMappings to be created inside the region
// with read permissions.  When on a VmMapping, controls whether or not the
// mapping can gain this permission.
#define VMAR_FLAG_CAN_MAP_READ (1 << 4)
// When on a VmAddressRegion, allow VmMappings to be created inside the region
// with write permissions.  When on a VmMapping, controls whether or not the
// mapping can gain this permission.
#define VMAR_FLAG_CAN_MAP_WRITE (1 << 5)
// When on a VmAddressRegion, allow VmMappings to be created inside the region
// with execute permissions.  When on a VmMapping, controls whether or not the
// mapping can gain this permission.
#define VMAR_FLAG_CAN_MAP_EXECUTE (1 << 6)
// Require that VMO backing the mapping is non-resizable.
#define VMAR_FLAG_REQUIRE_NON_RESIZABLE (1 << 7)
// Allow VMO backings that could result in faults.
#define VMAR_FLAG_ALLOW_FAULTS (1 << 8)
// Treat the offset as an upper limit when allocating a VMO or child VMAR.
#define VMAR_FLAG_OFFSET_IS_UPPER_LIMIT (1 << 9)

#define VMAR_CAN_RWX_FLAGS \
  (VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_CAN_MAP_WRITE | VMAR_FLAG_CAN_MAP_EXECUTE)

// forward declarations
class VmAddressRegion;
class VmMapping;
class VmEnumerator;

class PageRequest;

// A VmAddressRegion represents a contiguous region of the virtual address
// space.  It is partitioned by non-overlapping children of the following types:
// 1) child VmAddressRegion
// 2) child VmMapping (leafs that map VmObjects into the address space)
// 3) gaps (logical, not actually objects).
//
// VmAddressRegionOrMapping represents a tagged union of the two types.
//
// A VmAddressRegion/VmMapping may be in one of two states: ALIVE or DEAD.  If
// it is ALIVE, then the VmAddressRegion is a description of the virtual memory
// mappings of the address range it represents in its parent VmAspace.  If it is
// DEAD, then the VmAddressRegion is invalid and has no meaning.
//
// All VmAddressRegion and VmMapping state is protected by the aspace lock.
class VmAddressRegionOrMapping
    : public fbl::RefCounted<VmAddressRegionOrMapping>,
      public fbl::WAVLTreeContainable<fbl::RefPtr<VmAddressRegionOrMapping>> {
 public:
  // If a VMO-mapping, unmap all pages and remove dependency on vm object it has a ref to.
  // Otherwise recursively destroy child VMARs and transition to the DEAD state.
  //
  // Returns ZX_OK on success, ZX_ERR_BAD_STATE if already dead, and other
  // values on error (typically unmap failure).
  virtual zx_status_t Destroy();

  // accessors
  vaddr_t base() const { return base_; }
  size_t size() const { return size_; }
  uint32_t flags() const { return flags_; }
  const fbl::RefPtr<VmAspace>& aspace() const { return aspace_; }

  // Recursively compute the number of allocated pages within this region
  virtual size_t AllocatedPages() const;

  // Subtype information and safe down-casting
  bool is_mapping() const { return is_mapping_; }
  fbl::RefPtr<VmAddressRegion> as_vm_address_region();
  fbl::RefPtr<VmMapping> as_vm_mapping();
  VmAddressRegion* as_vm_address_region_ptr();
  VmMapping* as_vm_mapping_ptr();

  // Page fault in an address within the region.  Recursively traverses
  // the regions to find the target mapping, if it exists.
  // If this returns ZX_ERR_SHOULD_WAIT, then the caller should wait on |page_request|
  // and try again.
  virtual zx_status_t PageFault(vaddr_t va, uint pf_flags, PageRequest* page_request)
      TA_REQ(lock()) = 0;

  // WAVL tree key function
  vaddr_t GetKey() const { return base(); }

  // Dump debug info
  virtual void DumpLocked(uint depth, bool verbose) const TA_REQ(lock()) = 0;

  // Expose our backing lock for annotation purposes.
  Lock<Mutex>* lock() const TA_RET_CAP(aspace_->lock()) { return aspace_->lock(); }
  Lock<Mutex>& lock_ref() const TA_RET_CAP(aspace_->lock()) { return aspace_->lock_ref(); }

 private:
  fbl::Canary<fbl::magic("VMRM")> canary_;
  const bool is_mapping_;

 protected:
  // friend VmAddressRegion so it can access DestroyLocked
  friend VmAddressRegion;

  // destructor, should only be invoked from RefPtr
  virtual ~VmAddressRegionOrMapping();
  friend fbl::RefPtr<VmAddressRegionOrMapping>;

  bool in_subregion_tree() const {
    return fbl::WAVLTreeContainable<fbl::RefPtr<VmAddressRegionOrMapping>>::InContainer();
  }

  enum class LifeCycleState {
    // Initial state: if NOT_READY, then do not invoke Destroy() in the
    // destructor
    NOT_READY,
    // Usual state: information is representative of the address space layout
    ALIVE,
    // Object is invalid
    DEAD
  };

  VmAddressRegionOrMapping(vaddr_t base, size_t size, uint32_t flags, VmAspace* aspace,
                           VmAddressRegion* parent, bool is_mapping);

  // Check if the given *arch_mmu_flags* are allowed under this
  // regions *flags_*
  bool is_valid_mapping_flags(uint arch_mmu_flags);

  bool is_in_range(vaddr_t base, size_t size) const {
    const size_t offset = base - base_;
    return base >= base_ && offset < size_ && size_ - offset >= size;
  }

  // Returns true if the instance is alive and reporting information that
  // reflects the address space layout. |aspace()->lock()| must be held.
  bool IsAliveLocked() const;

  virtual zx_status_t DestroyLocked() TA_REQ(lock()) = 0;

  virtual size_t AllocatedPagesLocked() const TA_REQ(lock()) = 0;

  // Transition from NOT_READY to READY, and add references to self to related
  // structures.
  virtual void Activate() TA_REQ(lock()) = 0;

  // current state of the VMAR.  If LifeCycleState::DEAD, then all other
  // fields are invalid.
  LifeCycleState state_ = LifeCycleState::ALIVE;

  // address/size within the container address space
  vaddr_t base_;
  size_t size_;

  // flags from VMAR creation time
  const uint32_t flags_;

  // pointer back to our member address space.  The aspace's lock is used
  // to serialize all modifications.
  const fbl::RefPtr<VmAspace> aspace_;

  // pointer back to our parent region (nullptr if root or destroyed)
  VmAddressRegion* parent_;
};

// A list of regions ordered by virtual address. Templated to allow for test code to avoid needing
// to instantiate 'real' VmAddressRegionOrMapping instances.
template <typename T = VmAddressRegionOrMapping>
class RegionList final {
 public:
  using ChildList = fbl::WAVLTree<vaddr_t, fbl::RefPtr<T>>;

  // Remove *region* from the list, returns the removed region.
  fbl::RefPtr<T> RemoveRegion(T* region) { return regions_.erase(*region); }

  // Insert *region* to the region list.
  void InsertRegion(fbl::RefPtr<T> region) { regions_.insert(region); }

  // Find the region that covers addr, returns nullptr if not found.
  T* FindRegion(vaddr_t addr) const {
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

    return &*itr.CopyPointer();
  }

  // Find the region that contains |base|, or if that doesn't exist, the first region that contains
  // an address greater than |base|.
  typename ChildList::iterator IncludeOrHigher(vaddr_t base) {
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

  typename ChildList::iterator UpperBound(vaddr_t base) { return regions_.upper_bound(base); }

  // Check whether it would be valid to create a child in the range [base, base+size).
  bool IsRangeAvailable(vaddr_t base, size_t size) const {
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

  // Get the allocation spot that is free and large enough for the aligned size.
  zx_status_t GetAllocSpot(vaddr_t* alloc_spot, uint8_t align_pow2, uint8_t entropy, size_t size,
                           vaddr_t parent_base, size_t parent_size, crypto::PRNG* prng,
                           vaddr_t upper_limit = ktl::numeric_limits<vaddr_t>::max()) const {
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
    FindAllocSpotInGaps(size, align_pow2, selected_index, parent_base, parent_size,
                        &alloc_spot_info, upper_limit);
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

  // Utility for allocators for iterating over gaps between allocations.
  // F should have a signature of bool func(vaddr_t gap_base, size_t gap_size).
  // If func returns false, the iteration stops.  gap_base will be aligned in accordance with
  // align_pow2.
  template <typename F>
  void ForEachGap(F func, uint8_t align_pow2, vaddr_t parent_base, size_t parent_size) const {
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

  // Returns whether the region list is empty.
  bool IsEmpty() const { return regions_.is_empty(); }

  // Returns the iterator points to the first element of the list.
  T& front() { return regions_.front(); }

  typename ChildList::iterator begin() { return regions_.begin(); }

  typename ChildList::const_iterator begin() const { return regions_.begin(); }

  typename ChildList::const_iterator cbegin() const { return regions_.cbegin(); }

  typename ChildList::iterator end() { return regions_.end(); }

  typename ChildList::const_iterator end() const { return regions_.end(); }

  typename ChildList::const_iterator cend() const { return regions_.cend(); }

 private:
  // list of memory regions, indexed by base address.
  ChildList regions_;

  // A structure to contain allocated spot address or number of available slots.
  struct AllocSpotInfo {
    // candidate_spot_count is the number of available slot that we could allocate if we have not
    // found the spot with index |selected_index| to allocate.
    size_t candidate_spot_count = 0;
    // Found indicates whether we have found the spot with index |selected_indexes|.
    bool found = false;
    // alloc_spot is the virtual start address of the spot to allocate if we find one.
    vaddr_t alloc_spot = 0;
  };

  // Try to find the |selected_index| spot among all the gaps, alloc_spot_info contains the max
  // candidate spots if |selected_index| is larger than candidate_spaces. In this case, we need to
  // pick a smaller index and try again.
  void FindAllocSpotInGaps(size_t size, uint8_t align_pow2, vaddr_t selected_index,
                           vaddr_t parent_base, vaddr_t parent_size, AllocSpotInfo* alloc_spot_info,
                           vaddr_t upper_limit = ktl::numeric_limits<vaddr_t>::max()) const {
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
            // If we are able to find the spot with index |selected_indexes| in this gap, then we
            // have found our pick.
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

  // Compute the number of allocation spots that satisfy the alignment within the
  // given range size, for a range that has a base that satisfies the alignment.
  static constexpr size_t AllocationSpotsInRange(size_t range_size, size_t alloc_size,
                                                 uint8_t align_pow2) {
    return ((range_size - alloc_size) >> align_pow2) + 1;
  }

  // Returns the size of the given range clamped to the given upper limit. The base
  // of the range must be within the upper limit.
  static constexpr size_t ClampRange(vaddr_t range_base, size_t range_size, vaddr_t upper_limit) {
    DEBUG_ASSERT(range_base <= upper_limit);
    const size_t range_limit = range_base + range_size;
    return range_limit <= upper_limit ? range_size : range_size - (range_limit - upper_limit);
  }
};

// A representation of a contiguous range of virtual address space
class VmAddressRegion final : public VmAddressRegionOrMapping {
 public:
  // Create a root region.  This will span the entire aspace
  static zx_status_t CreateRoot(VmAspace& aspace, uint32_t vmar_flags,
                                fbl::RefPtr<VmAddressRegion>* out);
  // Create a subregion of this region
  zx_status_t CreateSubVmar(size_t offset, size_t size, uint8_t align_pow2, uint32_t vmar_flags,
                            const char* name, fbl::RefPtr<VmAddressRegion>* out);
  // Create a VmMapping within this region
  zx_status_t CreateVmMapping(size_t mapping_offset, size_t size, uint8_t align_pow2,
                              uint32_t vmar_flags, fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
                              uint arch_mmu_flags, const char* name, fbl::RefPtr<VmMapping>* out);

  // Find the child region that contains the given addr.  If addr is in a gap,
  // returns nullptr.  This is a non-recursive search.
  fbl::RefPtr<VmAddressRegionOrMapping> FindRegion(vaddr_t addr);

  // Apply |op| to VMO mappings in the specified range of pages.
  zx_status_t RangeOp(uint32_t op, size_t offset, size_t len, user_inout_ptr<void> buffer,
                      size_t buffer_size);

  // Unmap a subset of the region of memory in the containing address space,
  // returning it to this region to allocate.  If a subregion is entirely in
  // the range, that subregion is destroyed.  If a subregion is partially in
  // the range, Unmap() will fail.
  zx_status_t Unmap(vaddr_t base, size_t size);

  // Same as Unmap, but allows for subregions that are partially in the range.
  // Additionally, sub-VMARs that are completely within the range will not be
  // destroyed.
  zx_status_t UnmapAllowPartial(vaddr_t base, size_t size);

  // Change protections on a subset of the region of memory in the containing
  // address space.  If the requested range overlaps with a subregion,
  // Protect() will fail.
  zx_status_t Protect(vaddr_t base, size_t size, uint new_arch_mmu_flags);

  // Reserve a memory region within this VMAR. This region is already mapped in the page table with
  // |arch_mmu_flags|. VMAR should create a VmMapping for this region even though no physical pages
  // need to be allocated for this region.
  zx_status_t ReserveSpace(const char* name, size_t base, size_t size, uint arch_mmu_flags);

  const char* name() const { return name_; }
  bool has_parent() const;

  void DumpLocked(uint depth, bool verbose) const TA_REQ(lock()) override;
  zx_status_t PageFault(vaddr_t va, uint pf_flags, PageRequest* page_request)
      TA_REQ(lock()) override;

 protected:
  // constructor for use in creating a VmAddressRegionDummy
  explicit VmAddressRegion();

  friend class VmAspace;
  // constructor for use in creating the kernel aspace singleton
  explicit VmAddressRegion(VmAspace& kernel_aspace);
  // Count the allocated pages, caller must be holding the aspace lock
  size_t AllocatedPagesLocked() const TA_REQ(lock()) override;
  // Used to implement VmAspace::EnumerateChildren.
  // |aspace_->lock()| must be held.
  bool EnumerateChildrenLocked(VmEnumerator* ve, uint depth);

  friend class VmMapping;

  friend fbl::RefPtr<VmAddressRegion>;

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(VmAddressRegion);

  fbl::Canary<fbl::magic("VMAR")> canary_;

  // private constructors, use Create...() instead
  VmAddressRegion(VmAspace& aspace, vaddr_t base, size_t size, uint32_t vmar_flags);
  VmAddressRegion(VmAddressRegion& parent, vaddr_t base, size_t size, uint32_t vmar_flags,
                  const char* name);

  zx_status_t DestroyLocked() TA_REQ(lock()) override;

  void Activate() TA_REQ(lock()) override;

  // Helper to share code between CreateSubVmar and CreateVmMapping
  zx_status_t CreateSubVmarInternal(size_t offset, size_t size, uint8_t align_pow2,
                                    uint32_t vmar_flags, fbl::RefPtr<VmObject> vmo,
                                    uint64_t vmo_offset, uint arch_mmu_flags, const char* name,
                                    fbl::RefPtr<VmAddressRegionOrMapping>* out);

  // Create a new VmMapping within this region, overwriting any existing
  // mappings that are in the way.  If the range crosses a subregion, the call
  // fails.
  zx_status_t OverwriteVmMapping(vaddr_t base, size_t size, uint32_t vmar_flags,
                                 fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
                                 uint arch_mmu_flags, fbl::RefPtr<VmAddressRegionOrMapping>* out);

  // Implementation for Unmap() and OverwriteVmMapping() that does not hold
  // the aspace lock. If |can_destroy_regions| is true, then this may destroy
  // VMARs that it completely covers. If |allow_partial_vmar| is true, then
  // this can handle the situation where only part of the VMAR is contained
  // within the region and will not destroy any VMARs.
  zx_status_t UnmapInternalLocked(vaddr_t base, size_t size, bool can_destroy_regions,
                                  bool allow_partial_vmar);

  // returns true if we can meet the allocation between the given children,
  // and if so populates pva with the base address to use.
  bool CheckGapLocked(VmAddressRegionOrMapping* prev, VmAddressRegionOrMapping* next, vaddr_t* pva,
                      vaddr_t search_base, vaddr_t align, size_t region_size, size_t min_gap,
                      uint arch_mmu_flags);

  // search for a spot to allocate for a region of a given size
  zx_status_t AllocSpotLocked(size_t size, uint8_t align_pow2, uint arch_mmu_flags, vaddr_t* spot,
                              vaddr_t upper_limit = ktl::numeric_limits<vaddr_t>::max());

  RegionList<VmAddressRegionOrMapping> subregions_;

  const char name_[32] = {};
};

// A representation of the mapping of a VMO into the address space
class VmMapping final : public VmAddressRegionOrMapping,
                        public fbl::DoublyLinkedListable<VmMapping*> {
 public:
  // Accessors for VMO-mapping state
  // These can be read under either lock (both locks being held for writing), so we provide two
  // different accessors, one for each lock.
  uint arch_mmu_flags_locked() const TA_REQ(aspace_->lock()) TA_NO_THREAD_SAFETY_ANALYSIS {
    return arch_mmu_flags_;
  }
  uint arch_mmu_flags_locked_object() const TA_REQ(object_->lock()) TA_NO_THREAD_SAFETY_ANALYSIS {
    return arch_mmu_flags_;
  }
  uint64_t object_offset_locked() const TA_REQ(lock()) TA_NO_THREAD_SAFETY_ANALYSIS {
    return object_offset_;
  }
  uint64_t object_offset_locked_object() const
      TA_REQ(object_->lock()) TA_NO_THREAD_SAFETY_ANALYSIS {
    return object_offset_;
  }
  // Intended to be used from VmEnumerator callbacks where the aspace_->lock() will be held.
  fbl::RefPtr<VmObject> vmo_locked() const { return object_; }
  fbl::RefPtr<VmObject> vmo() const;

  // Convenience wrapper for vmo()->DecommitRange() with the necessary
  // offset modification and locking.
  zx_status_t DecommitRange(size_t offset, size_t len);

  // Map in pages from the underlying vm object, optionally committing pages as it goes
  zx_status_t MapRange(size_t offset, size_t len, bool commit);
  zx_status_t MapRangeLocked(size_t offset, size_t len, bool commit) TA_REQ(lock());

  // Unmap a subset of the region of memory in the containing address space,
  // returning it to the parent region to allocate.  If all of the memory is unmapped,
  // Destroy()s this mapping.  If a subrange of the mapping is specified, the
  // mapping may be split.
  zx_status_t Unmap(vaddr_t base, size_t size);

  // Change access permissions for this mapping.  It is an error to specify a
  // caching mode in the flags.  This will persist the caching mode the
  // mapping was created with.  If a subrange of the mapping is specified, the
  // mapping may be split.
  zx_status_t Protect(vaddr_t base, size_t size, uint new_arch_mmu_flags);

  void DumpLocked(uint depth, bool verbose) const TA_REQ(lock()) override;
  zx_status_t PageFault(vaddr_t va, uint pf_flags, PageRequest* page_request)
      TA_REQ(lock()) override;

  // Apis intended for use by VmObject

  Lock<Mutex>* object_lock() TA_RET_CAP(object_->lock()) { return object_->lock(); }

  // Unmap any pages that map the passed in vmo range from the arch aspace.
  // May not intersect with this range.
  zx_status_t AspaceUnmapVmoRangeLocked(uint64_t offset, uint64_t len) const
      TA_REQ(object_->lock());

  // Removes any writeable mappings for the passed in vmo range from the arch aspace.
  // May fall back to unmapping pages from the arch aspace if necessary.
  zx_status_t AspaceRemoveWriteVmoRangeLocked(uint64_t offset, uint64_t len) const
      TA_REQ(object_->lock());

  // Harvests accessed bits for any pages in the passed in vmo range and calls the provided callback
  // for any pages with a bit to harvest.
  zx_status_t HarvestAccessVmoRangeLocked(
      uint64_t offset, uint64_t len,
      const fbl::Function<bool(vm_page*, uint64_t vmo_offset)>& accessed_callback) const
      TA_REQ(object_->lock());

  // Used to cache the page attribution count for this vmo range. Also tracks the vmo hierarchy
  // generation count and the mapping generation count at the time of caching the attributed page
  // count.
  struct CachedPageAttribution {
    uint64_t mapping_generation_count = 0;
    uint64_t vmo_generation_count = 0;
    size_t page_count = 0;
  };

  // Exposed for testing.
  CachedPageAttribution GetCachedPageAttribution() {
    Guard<Mutex> guard{aspace_->lock()};
    return cached_page_attribution_;
  }

  // Exposed for testing.
  uint64_t GetMappingGenerationCount() {
    Guard<Mutex> guard{aspace_->lock()};
    return GetMappingGenerationCountLocked();
  }

 protected:
  ~VmMapping() override;
  friend fbl::RefPtr<VmMapping>;

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(VmMapping);

  fbl::Canary<fbl::magic("VMAP")> canary_;

  // allow VmAddressRegion to manipulate VmMapping internals for construction
  // and bookkeeping
  friend class VmAddressRegion;

  // private constructors, use VmAddressRegion::Create...() instead
  VmMapping(VmAddressRegion& parent, vaddr_t base, size_t size, uint32_t vmar_flags,
            fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset, uint arch_mmu_flags);

  zx_status_t DestroyLocked() TA_REQ(lock()) override;

  // Implementation for Unmap().  This supports partial unmapping.
  zx_status_t UnmapLocked(vaddr_t base, size_t size) TA_REQ(lock());

  // Implementation for Protect().
  zx_status_t ProtectLocked(vaddr_t base, size_t size, uint new_arch_mmu_flags) TA_REQ(lock());

  size_t AllocatedPagesLocked() const TA_REQ(lock()) override;

  void Activate() TA_REQ(lock()) override;

  void ActivateLocked() TA_REQ(lock()) TA_REQ(object_->lock());

  // Takes a range relative to the vmo object_ and converts it into a virtual address range relative
  // to aspace_. Returns true if a non zero sized intersection was found, false otherwise. If false
  // is returned |base| and |virtual_len| hold undefined contents.
  bool ObjectRangeToVaddrRange(uint64_t offset, uint64_t len, vaddr_t* base,
                               uint64_t* virtual_len) const TA_REQ(object_->lock());

  // This should be called whenever a change is made to the vmo range we are mapping, that could
  // result in the page attribution count of that range changing.
  void IncrementMappingGenerationCountLocked() TA_REQ(lock()) {
    DEBUG_ASSERT(mapping_generation_count_ != 0);
    mapping_generation_count_++;
  }

  // Get the current generation count.
  uint64_t GetMappingGenerationCountLocked() const TA_REQ(lock()) {
    DEBUG_ASSERT(mapping_generation_count_ != 0);
    return mapping_generation_count_;
  }

  // Helper function that updates the |size_| to |new_size| and also increments the mapping
  // generation count. Requires both the aspace lock and the object lock to be held, since |size_|
  // can be read under either of those locks.
  void set_size_locked(size_t new_size) TA_REQ(lock()) TA_REQ(object_->lock()) {
    size_ = new_size;
    IncrementMappingGenerationCountLocked();
  }

  // pointer and region of the object we are mapping
  fbl::RefPtr<VmObject> object_;
  // This can be read with either lock hold, but requires both locks to write it.
  uint64_t object_offset_ TA_GUARDED(object_->lock()) TA_GUARDED(aspace_->lock()) = 0;

  // cached mapping flags (read/write/user/etc).
  // This can be read with either lock hold, but requires both locks to write it.
  uint arch_mmu_flags_ TA_GUARDED(object_->lock()) TA_GUARDED(aspace_->lock());

  // used to detect recursions through the vmo fault path
  bool currently_faulting_ TA_GUARDED(object_->lock()) = false;

  // Tracks the last cached page attribution count for the vmo range we are mapping.
  // Only used when |object_| is a VmObjectPaged.
  mutable CachedPageAttribution cached_page_attribution_ TA_GUARDED(aspace_->lock()) = {};

  // The mapping's generation count is incremented on any change to the vmo range that is mapped.
  //
  // This is used to implement caching for page attribution counts, which get queried frequently to
  // periodically track memory usage on the system. Attributing pages to a VMO is an expensive
  // operation and involves walking the VMO tree, quite often multiple times. If the generation
  // counts for the vmo *and* the mapping do not change between two successive queries, we can avoid
  // re-counting attributed pages, and simply return the previously cached value.
  //
  // The generation count starts at 1 to ensure that there can be no cached values initially; the
  // cached generation count starts at 0.
  uint64_t mapping_generation_count_ TA_GUARDED(aspace_->lock()) = 1;
};

// Interface for walking a VmAspace-rooted VmAddressRegion/VmMapping tree.
// Override this class and pass an instance to VmAspace::EnumerateChildren().
class VmEnumerator {
 public:
  // VmAspace::EnumerateChildren() will call the On* methods in depth-first
  // pre-order. If any call returns false, the traversal will stop. The root
  // VmAspace's lock will be held during the entire traversal.
  // |depth| will be 0 for the root VmAddressRegion.
  virtual bool OnVmAddressRegion(const VmAddressRegion* vmar, uint depth) TA_REQ(vmar->lock()) {
    return true;
  }

  // |vmar| is the parent of |map|. The root VmAspace's lock will be held when this is called.
  virtual bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar, uint depth)
      TA_REQ(map->lock()) TA_REQ(vmar->lock()) {
    return true;
  }

 protected:
  VmEnumerator() = default;
  ~VmEnumerator() = default;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_ADDRESS_REGION_H_
