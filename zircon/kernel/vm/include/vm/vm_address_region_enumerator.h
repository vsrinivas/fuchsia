// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_VM_ADDRESS_REGION_ENUMERATOR_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_VM_ADDRESS_REGION_ENUMERATOR_H_

#include <assert.h>
#include <stdint.h>
#include <zircon/types.h>

#include <ktl/optional.h>
#include <ktl/type_traits.h>
#include <vm/vm_address_region.h>

enum class VmAddressRegionEnumeratorType : bool {
  // If the enumeration will never be paused then both regions and mappings can be yielded.
  UnpausableVmarOrMapping = false,
  // If the enumeration supports pausing then only mappings will be yielded. This is necessary to
  // ensure forward progress.
  PausableMapping = true,
};

// Helper class for performing enumeration of a VMAR. The enumeration type is a template to help
// statically prevent misuse and enforce unique return types. Although this is intended to be
// internal, it is declared here for exposing to unit tests.
//
// The purpose of having a stateful enumerator is to have the option to not need to hold the aspace
// lock over the entire enumeration, whilst guaranteeing forward progress and termination. If the
// vmar is modified whilst enumeration is paused (due to dropping the lock or otherwise) then it is
// not well defined whether the enumerator will return any new mappings. However, the enumerator
// will never return DEAD mappings, and will not return mappings in ranges it has already
// enumerated.
//
// Except between calls to |pause| and |resume|, the vmar should be considered immutable, and
// sub-vmars and mappings should not be modified.
template <VmAddressRegionEnumeratorType Type>
class VmAddressRegionEnumerator {
 public:
  // This requires the vmar lock to be held over the lifetime of the object, except where explicitly
  // stated otherwise.
  VmAddressRegionEnumerator(VmAddressRegion& vmar, vaddr_t min_addr, vaddr_t max_addr)
      TA_REQ(vmar.lock())
      : min_addr_(min_addr),
        max_addr_(max_addr),
        vmar_(vmar),
        itr_(vmar_.subregions_.IncludeOrHigher(min_addr_)) {}

  struct NextResult {
    using RegionOrMapping =
        ktl::conditional_t<Type == VmAddressRegionEnumeratorType::PausableMapping, VmMapping,
                           VmAddressRegionOrMapping>;
    RegionOrMapping* region_or_mapping;
    uint depth;
  };

  // Yield the next region or mapping, or a nullopt if enumeration has completed. Regions are
  // yielded in depth-first pre-order.
  ktl::optional<NextResult> next() TA_REQ(vmar_.lock()) {
    if constexpr (Type == VmAddressRegionEnumeratorType::PausableMapping) {
      ASSERT(!state_.paused_);
    }
    ktl::optional<NextResult> ret = ktl::nullopt;
    while (!ret && itr_.IsValid() && itr_->base() < max_addr_) {
      DEBUG_ASSERT(itr_->IsAliveLocked());
      auto curr = itr_++;
      VmAddressRegion* up = curr->parent_;

      if (curr->is_mapping()) {
        VmMapping* mapping = curr->as_vm_mapping().get();

        DEBUG_ASSERT(mapping != nullptr);
        AssertHeld(mapping->lock_ref());
        // If the mapping is entirely before |min_addr| or entirely after |max_addr| do not run
        // on_mapping. This can happen when a vmar contains min_addr but has mappings entirely
        // below it, for example.
        if ((mapping->base() < min_addr_ && mapping->base() + mapping->size() <= min_addr_) ||
            mapping->base() > max_addr_) {
          continue;
        }
        ret = NextResult{mapping, depth_};
      } else {
        VmAddressRegion* vmar = curr->as_vm_address_region().get();
        DEBUG_ASSERT(vmar != nullptr);
        AssertHeld(vmar->lock_ref());
        if constexpr (Type == VmAddressRegionEnumeratorType::UnpausableVmarOrMapping) {
          ret = NextResult{vmar, depth_};
        }
        if (!vmar->subregions_.IsEmpty()) {
          // If the sub-VMAR is not empty, iterate through its children.
          itr_ = vmar->subregions_.begin();
          depth_++;
          continue;
        }
      }
      if (depth_ > kStartDepth && !itr_.IsValid()) {
        AssertHeld(up->lock_ref());
        // If we are at a depth greater than the minimum, and have reached
        // the end of a sub-VMAR range, we ascend and continue iteration.
        do {
          itr_ = up->subregions_.UpperBound(curr->base());
          if (itr_.IsValid()) {
            break;
          }
          up = up->parent_;
        } while (depth_-- != kStartDepth);
        if (!itr_.IsValid()) {
          // If we have reached the end after ascending all the way up,
          // break out of the loop.
          break;
        }
      }
    }
    return ret;
  }

  // Pause enumeration. Until |resume| is called |next| may not be called, but the vmar lock is
  // permitted to be dropped, and the vmar is permitted to be modified.
  void pause() TA_REQ(vmar_.lock()) {
    static_assert(Type == VmAddressRegionEnumeratorType::PausableMapping);
    ASSERT(!state_.paused_);
    // Save information of the next iteration we should return.
    if (itr_.IsValid()) {
      // Per comment on |itr_|, we could be at a VmAddressRegion or a VmMapping. However, a
      // VmAddressRegion (or a VmMapping with a base below min_addr_) is only possible if we have
      // just constructed the enumerator, or just called |resume| (without calling |next|). We do
      // not track specifically if we have called |next| or not, but we do know that if depth_ is
      // not kStartDepth, then |next| must have been called. Using the depth_ heuristic we have at
      // least a chance of detecting incorrect enumerations with the following assert.
      DEBUG_ASSERT((itr_->is_mapping() && itr_->base() >= min_addr_) || depth_ == kStartDepth);
      // Is possible that the object extends only partially into our enumeration range. As such we
      // cannot just record its base() as the point to resume iteration, but need to clip it with
      // |min_addr_| to ensure we do not iterate backwards or outside of our requested range.
      state_.next_offset_ = ktl::max(min_addr_, itr_->base());
      state_.region_or_mapping_ = itr_.CopyPointer();
    } else {
      state_.next_offset_ = max_addr_;
      state_.region_or_mapping_ = nullptr;
    }
    state_.paused_ = true;
  }

  // Resume enumeration allowing |next| to be called again.
  void resume() TA_REQ(vmar_.lock()) {
    static_assert(Type == VmAddressRegionEnumeratorType::PausableMapping);
    ASSERT(state_.paused_);
    if (state_.region_or_mapping_) {
      if (!itr_->IsAliveLocked()) {
        // Generate a new iterator that starts at the right offset, but back at the top. The next
        // call to next() will walk back down if necessary to find a mapping.
        min_addr_ = state_.next_offset_;
        itr_ = vmar_.subregions_.IncludeOrHigher(min_addr_);
        depth_ = kStartDepth;
      } else {
        DEBUG_ASSERT(&*itr_ == &*state_.region_or_mapping_);
      }
      // Free the refptr. Note that the actual destructors of VmAddressRegionOrMapping objects
      // themselves do very little, so we are safe to potential invoke the destructor here.
      state_.region_or_mapping_.reset();
    } else {
      // There was no refptr, meaning itr_ was already not valid, and should still not be valid.
      ASSERT(!itr_.IsValid());
    }
    state_.paused_ = false;
  }

  // Expose our backing lock for annotation purposes.
  Lock<Mutex>& lock_ref() const TA_RET_CAP(vmar_.lock_ref()) { return vmar_.lock_ref(); }

 private:
  struct PauseState {
    bool paused_ = false;
    vaddr_t next_offset_ = 0;
    fbl::RefPtr<VmAddressRegionOrMapping> region_or_mapping_;
  };
  struct NoPauseState {};

  using StateType = ktl::conditional_t<Type == VmAddressRegionEnumeratorType::PausableMapping,
                                       PauseState, NoPauseState>;
  StateType state_;
  vaddr_t min_addr_;
  const vaddr_t max_addr_;
  static constexpr uint kStartDepth = 1;
  uint depth_ = kStartDepth;
  // Root vmar being enumerated.
  VmAddressRegion& vmar_;
  // This iterator represents the object at which |next| should use to find the next item to return.
  // Regardless of the kind of enumerator this might be a reference to either a VmAddressRegion or
  // a VmMapping. Although PausableMapping only yields VmMappings, it may still need to have its
  // itr_ point to a VmAddressRegion at the point of construction, or after a |resume|.
  // An invalid itr_ therefore represents no next object, and means enumeration has finished.
  RegionList<VmAddressRegionOrMapping>::ChildList::iterator itr_;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_ADDRESS_REGION_ENUMERATOR_H_
