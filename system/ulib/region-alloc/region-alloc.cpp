// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/algorithm.h>
#include <region-alloc/region-alloc.h>
#include <string.h>

// Support for Pool allocated bookkeeping
RegionAllocator::RegionPool::RefPtr RegionAllocator::RegionPool::Create(size_t slab_size,
                                                                        size_t max_memory) {
    // Sanity check our allocation arguments.
    if ((slab_size < sizeof(RegionAllocator::RegionPool::Slab)) || (slab_size > max_memory))
        return RefPtr(nullptr);

    RefPtr ret = mxtl::AdoptRef(new RegionPool(slab_size, max_memory));

    // Allocate at least one slab to start with.
    if (ret != nullptr)
        ret->Grow();

    return ret;
}

RegionAllocator::RegionPool::~RegionPool() {
#if LK_DEBUGLEVEL > 1
    ASSERT(!in_flight_allocations_);
#endif
    free_regions_.clear();
    slabs_.clear();
}

void RegionAllocator::RegionPool::Grow() {
    DEBUG_ASSERT(alloc_size_ <= max_memory_);

    // If growing would put us over our max_memory limit, do not grow.
    if ((alloc_size_ + slab_size_) > max_memory_)
        return;

    // Attempt to allocate a new slab.  Do not continue to run if we cannot.
    DEBUG_ASSERT(slab_size_ >= sizeof(RegionAllocator::RegionPool::Slab));
    Slab::UPtr slab(new (slab_size_) Slab());
    if (!slab)
        return;

    // We have successfully allocated a new slab.  Carve up its memory into
    // Regions and place them on free list so they are available for allocation.
    static_assert(offsetof(Slab, mem_) == sizeof(Slab), "mem_ must be the final member of Slab!");
    size_t count = (slab_size_ - sizeof(Slab)) / sizeof(Region);
    Region* tmp = reinterpret_cast<Region*>(slab->mem_);
    for (size_t i = 0; i < count; ++i) {
        Region* free_region = new (tmp + i) Region();
        SlabEntry* free_entry = reinterpret_cast<SlabEntry*>(free_region);
        DEBUG_ASSERT(!free_entry->sll_node_state_.InContainer());
        DEBUG_ASSERT( free_entry->sll_node_state_.IsValid());

        free_regions_.push_front(free_entry);
    }

    // Add it to the list of slabs allocated slabs so that it can be cleaned up
    // at shutdown time.  Also update our allocation bookkeeping.
    slabs_.push_front(mxtl::move(slab));
    alloc_size_ += slab_size_;
}

RegionAllocator::Region* RegionAllocator::RegionPool::AllocRegion(RegionAllocator* owner) {
    if (free_regions_.is_empty()) {
        Grow();
        if (free_regions_.is_empty())
            return nullptr;
    }

#if LK_DEBUGLEVEL > 1
    ++in_flight_allocations_;
#endif

    auto entry = free_regions_.pop_front();
    DEBUG_ASSERT(!entry->sll_node_state_.InContainer());
    DEBUG_ASSERT( entry->sll_node_state_.IsValid());

    Region* region = reinterpret_cast<Region*>(entry);

    DEBUG_ASSERT(!region->ns_tree_sort_by_base_.InContainer());
    DEBUG_ASSERT(!region->ns_tree_sort_by_size_.InContainer());
    DEBUG_ASSERT( region->ns_tree_sort_by_base_.IsValid());
    DEBUG_ASSERT( region->ns_tree_sort_by_size_.IsValid());

    region->owner_ = owner;
    return region;
}

void RegionAllocator::RegionPool::FreeRegion(Region* region) {
    DEBUG_ASSERT(region);
    DEBUG_ASSERT(!region->ns_tree_sort_by_base_.InContainer());
    DEBUG_ASSERT(!region->ns_tree_sort_by_size_.InContainer());
    DEBUG_ASSERT( region->ns_tree_sort_by_base_.IsValid());
    DEBUG_ASSERT( region->ns_tree_sort_by_size_.IsValid());
    ::memset(region, 0, sizeof(*region));

#if LK_DEBUGLEVEL > 1
    ASSERT(in_flight_allocations_);
    --in_flight_allocations_;
#endif

    SlabEntry* free_entry = reinterpret_cast<SlabEntry*>(region);
    DEBUG_ASSERT(!free_entry->sll_node_state_.InContainer());
    DEBUG_ASSERT( free_entry->sll_node_state_.IsValid());
    free_regions_.push_front(free_entry);
}

RegionAllocator::~RegionAllocator() {
    // No one should be destroying us while we have allocations in flight.
    DEBUG_ASSERT(allocated_regions_by_base_.is_empty());

    // We should have the same number of regions sorted by base address and by
    // size.
    DEBUG_ASSERT(avail_regions_by_base_.size() == avail_regions_by_size_.size());

    // We have to have a region pool assigned to us
    DEBUG_ASSERT(region_pool_ != nullptr);

    // Return all of our bookkeeping to our region pool.
    avail_regions_by_base_.clear();
    while (!avail_regions_by_size_.is_empty())
        region_pool_->FreeRegion(avail_regions_by_size_.pop_front());
}

void RegionAllocator::Reset() {
    DEBUG_ASSERT((region_pool_ != nullptr) || avail_regions_by_base_.is_empty());

    Region* removed;
    while ((removed = avail_regions_by_base_.pop_front()) != nullptr) {
        avail_regions_by_size_.erase(*removed);
        region_pool_->FreeRegion(removed);
    }

    DEBUG_ASSERT(avail_regions_by_base_.is_empty());
    DEBUG_ASSERT(avail_regions_by_size_.is_empty());
}

mx_status_t RegionAllocator::SetRegionPool(const RegionPool::RefPtr& region_pool) {
    if (!allocated_regions_by_base_.is_empty() || !avail_regions_by_base_.is_empty())
        return ERR_BAD_STATE;

    region_pool_ = region_pool;
    return NO_ERROR;
}

mx_status_t RegionAllocator::AddRegion(const ralloc_region_t& region, bool allow_overlap) {
    // Start with sanity checks
    mx_status_t ret = AddSubtractSanityCheck(region);
    if (ret != NO_ERROR)
        return ret;

    // Make sure that we do not intersect with the available regions if we do
    // not allow overlaps.
    if (!allow_overlap && Intersects(avail_regions_by_base_, region))
        return ERR_INVALID_ARGS;

    // All sanity checks passed.  Grab a piece of free bookeeping from our pool,
    // fill it out, then add it to the sets of available regions (indexed by
    // base address as well as size)
    Region* to_add = region_pool_->AllocRegion(this);
    if (to_add == nullptr)
        return ERR_NO_MEMORY;

    to_add->base = region.base;
    to_add->size = region.size;

    AddRegionToAvail(to_add, allow_overlap);

    return NO_ERROR;
}

mx_status_t RegionAllocator::SubtractRegion(const ralloc_region_t& to_subtract,
                                            bool allow_incomplete) {
    // Start with sanity checks
    mx_status_t ret = AddSubtractSanityCheck(to_subtract);
    if (ret != NO_ERROR)
        return ret;

    // Make a copy of the region to subtract.  We may need to modify the region
    // as part of the subtraction algorithm.
    ralloc_region_t region = to_subtract;

    // Find the region whose base address is <= the specified region (if any).
    // If we do not allow incomplete subtraction, this is the region which must
    // entirely contain the subtracted region.
    //
    // Additionally, the only time we should ever need any extra bookkeeping
    // allocation is if this region (if present) needs to be split into two
    // regions.
    auto before = avail_regions_by_base_.upper_bound(region.base);
    auto after  = before--;
    uint64_t region_end = region.base + region.size;  // exclusive end
    uint64_t before_end = 0;

    if (before.IsValid()) {
        before_end = before->base + before->size;  // exclusive end
        if ((region.base >= before->base) && (region_end <= before_end)) {
            // Looks like we found an available region which completely contains the
            // region to be subtracted.  Handle the 4 possible cases.

            // Case 1: The regions are the same.  This one is easy.
            if ((region.base == before->base) && (region_end == before_end)) {
                Region* removed = avail_regions_by_base_.erase(before);
                avail_regions_by_size_.erase(*removed);
                region_pool_->FreeRegion(removed);
                return NO_ERROR;
            }

            // Case 2: before completely contains region.  The before region needs
            // to be split into two regions.  If we are out of bookkeeping space, we
            // are out of luck.
            if ((region.base != before->base) && (region_end != before_end)) {
                Region* second = region_pool_->AllocRegion(this);
                if (second == nullptr)
                    return ERR_NO_MEMORY;

                // Looks like we have the memory we need.  Compute the base/size of
                // the two regions which will be left over, then update the first
                // region's position in the size index, and add the second region to
                // the set of available regions.
                Region* first = avail_regions_by_size_.erase(*before);
                first->size  = region.base - first->base;
                second->base = region_end;
                second->size = before_end - region_end;

                avail_regions_by_size_.insert(first);
                avail_regions_by_base_.insert(second);
                avail_regions_by_size_.insert(second);
                return NO_ERROR;
            }

            // Case 3: region trims the front of before.  Update before's base and
            // size, and recompute its position in the size index.  Note: there is
            // no need to recompute its position in the base index, this has not
            // changed.
            if (region.base == before->base) {
                DEBUG_ASSERT(region_end < before_end);

                Region* bptr = avail_regions_by_size_.erase(*before);
                bptr->base += region.size;
                bptr->size -= region.size;
                avail_regions_by_size_.insert(bptr);

                return NO_ERROR;
            }

            // Case 4: region trims the end of before.  Update before's size and
            // recompute its position in the size index.
            DEBUG_ASSERT(region.base != before->base);
            DEBUG_ASSERT(region_end == before_end);

            Region* bptr = avail_regions_by_size_.erase(*before);
            bptr->size -= region.size;
            avail_regions_by_size_.insert(bptr);

            return NO_ERROR;
        }
    }

    // If we have gotten this far, then there is no single region in the
    // available set which completely contains the subtraction region.  We
    // cannot continue unless allow_incomplete is true.
    if (!allow_incomplete)
        return ERR_INVALID_ARGS;

    // Great!  At this point we know that we are going to succeed, we just need
    // to go about updating all of the bookkeeping.  We may need to trim the end
    // of the region which comes before us, and then consume some of the regions
    // which come after us, finishing by trimming the front of (at most) one of
    // the regions which comes after us.  At no point in time do we need to
    // allocate any more bookkeeping, success is guaranteed.  Start by
    // considering the before region.
    if (before.IsValid()) {
        DEBUG_ASSERT(region.base >= before->base);
        DEBUG_ASSERT(region_end  >  before_end);
        if (before_end > region.base) {
            // No matter what, 'before' needs to be removed from the size index.
            Region* bptr = avail_regions_by_size_.erase(*before);

            // If before's base is the same as the region's base, then we are
            // subtracting out all of before.  Otherwise, we are trimming the back
            // before and need to recompute its size and position in the size index.
            if (bptr->base == region.base) {
                avail_regions_by_base_.erase(*bptr);
                region_pool_->FreeRegion(bptr);
            } else {
                bptr->size = region.base - bptr->base;
                avail_regions_by_size_.insert(bptr);
            }

            // Either way, the region we are subtracting now starts where before
            // used to end.
            region.base = before_end;
            region.size = region_end - region.base;
            DEBUG_ASSERT(region.size > 0);
        }
    }

    // While there are regions whose base address comes after the base address
    // of what we want to subtract, we need to do one of three things...
    //
    // 1) Consume entire regions which are contained entirely within our
    //    subtraction region.
    // 2) Trim the front of a region which is clipped by our subtraction region.
    // 3) Stop because all remaining regions start after the end of our
    //    subtraction region.
    while (after.IsValid()) {
        DEBUG_ASSERT(after->base > region.base);

        // Case #3
        if (after->base >= region_end)
            break;

        // Cases #1 and #2.  No matter what, we need to...
        // 1) Advance after, re-naming the old 'after' to 'trim in the process.
        // 2) Remove trim from the size index.
        auto     trim_iter = after++;
        Region*  trim      = avail_regions_by_size_.erase(*trim_iter);
        uint64_t trim_end  = trim->base + trim->size;

        if (trim_end > region_end) {
            // Case #2.  We are guaranteed to be done at this point.
            trim->base = region_end;
            trim->size = trim_end - trim->base;
            avail_regions_by_size_.insert(trim);
            break;
        }

        // Case #1.  Advance the subtraction region to the end of the region we
        // just trimmed into oblivion.  If we have run out of region to trim,
        // then we know we are done.
        avail_regions_by_base_.erase(*trim);
        region.base = trim_end;
        region.size = region_end - region.base;
        region_pool_->FreeRegion(trim);

        if (!region.size)
            break;
    }


    // Sanity check.  The number of elements in the base index should match the
    // number of elements in the size index.
    DEBUG_ASSERT(avail_regions_by_base_.size() == avail_regions_by_size_.size());
    return NO_ERROR;
}

mx_status_t RegionAllocator::GetRegion(uint64_t size,
                                       uint64_t alignment,
                                       Region::UPtr& out_region) {
    // Check our RegionPool
    if (region_pool_ == nullptr)
        return ERR_BAD_STATE;

    // Sanity check the arguments.
    out_region = nullptr;
    if (!size || !alignment || !mxtl::is_pow2(alignment))
        return ERR_INVALID_ARGS;

    // Compute the things we will need round-up align base addresses.
    uint64_t mask     = alignment - 1;
    uint64_t inv_mask = ~mask;

    // Start by using our size index to look up the first available region which
    // is large enough to hold this allocation (if any)
    auto iter = avail_regions_by_size_.lower_bound({ .base = 0, .size = size });

    // Consider all of the regions which are large enough to hold our
    // allocation.  Stop as soon as we find one which can satisfy the alignment
    // restrictions.
    uint64_t aligned_base;
    while (iter.IsValid()) {
        DEBUG_ASSERT(iter->size >= size);
        aligned_base = (iter->base + mask) & inv_mask;
        uint64_t overhead = aligned_base - iter->base;
        uint64_t leftover = iter->size - size;

        // We have a usable region if the aligned base address has not wrapped
        // the address space, and if overhead required to align the allocation
        // is not larger than what is leftover in the region after performing
        // the allocation.
        if ((aligned_base >= iter->base) && (overhead <= leftover))
            break;

        ++iter;
    }

    if (!iter.IsValid())
        return ERR_NOT_FOUND;

    return AllocFromAvail(iter, out_region, aligned_base, size);
}

mx_status_t RegionAllocator::GetRegion(const ralloc_region_t& requested_region,
                                       Region::UPtr& out_region) {
    // Check our RegionPool
    if (region_pool_ == nullptr)
        return ERR_BAD_STATE;

    uint64_t base = requested_region.base;
    uint64_t size = requested_region.size;

    // Sanity check the arguments.
    out_region = nullptr;
    if (!size || ((base + size) < base))
        return ERR_INVALID_ARGS;

    // Find the first available region whose base address is strictly greater
    // than the one we are looking for, then back up one.
    auto iter = avail_regions_by_base_.upper_bound(base);
    --iter;

    // If the iterator is invalid, then we cannot satisfy this request.  If it
    // is valid, then we can satisfy this request if and only if the region we
    // found completely contains the requested region.
    if (!iter.IsValid())
        return ERR_NOT_FOUND;

    // We know that base must be >= iter->base
    // We know that iter->size is non-zero.
    // Therefore, we know that base is in the range [iter.start, iter.end]
    // We know that request.end is > base
    // Therefore request.end is > iter.base
    //
    // So, if request.end <= iter.end, we know that request is completely
    // contained within iter.  It does not matter if we use the inclusive or
    // exclusive end to check, as long as we are consistent.
    DEBUG_ASSERT(iter->size > 0);
    DEBUG_ASSERT(iter->base <= base);
    uint64_t req_end  = base + size - 1;
    uint64_t iter_end = iter->base + iter->size - 1;
    if (req_end > iter_end)
        return ERR_NOT_FOUND;

    // Great, we have found a region which should be able to satisfy our
    // allocation request.  Get an iterator for the by-size index, then use the
    // common AllocFromAvail method to handle the bookkeeping involved.
    auto by_size_iter = avail_regions_by_size_.make_iterator(*iter);
    return AllocFromAvail(by_size_iter, out_region, base, size);
}

mx_status_t RegionAllocator::AddSubtractSanityCheck(const ralloc_region_t& region) {
    // Check our RegionPool
    if (region_pool_ == nullptr)
        return ERR_BAD_STATE;

    // Sanity check the region to make sure that it is well formed.  We do not
    // allow a region which is of size zero, or which wraps around the
    // allocation space.
    if ((region.base + region.size) <= region.base)
        return ERR_INVALID_ARGS;

    // Next, make sure the region we are adding or subtracting does not
    // intersect any region which is currently allocated.
    if (Intersects(allocated_regions_by_base_, region))
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}

void RegionAllocator::ReleaseRegion(Region* region) {
    DEBUG_ASSERT(region != nullptr);

    // When a region comes back from a user, it should be in the
    // allocated_regions_by_base tree, but not in either of the avail_regions
    // trees, and not in any free list.  Remove it from the allocated_regions
    // bookkeeping and add it back to the available regions.
    DEBUG_ASSERT(region->ns_tree_sort_by_base_.InContainer());
    DEBUG_ASSERT(!region->ns_tree_sort_by_size_.InContainer());

    allocated_regions_by_base_.erase(*region);
    AddRegionToAvail(region);
}

mx_status_t RegionAllocator::AllocFromAvail(Region::WAVLTreeSortBySize::iterator source,
                                            Region::UPtr& out_region,
                                            uint64_t base,
                                            uint64_t size) {
    DEBUG_ASSERT(out_region == nullptr);
    DEBUG_ASSERT(source.IsValid());
    DEBUG_ASSERT(base >= source->base);
    DEBUG_ASSERT(size <= source->size);

    uint64_t overhead = base - source->base;
    DEBUG_ASSERT(overhead < source->size);

    uint64_t leftover = source->size - size;
    DEBUG_ASSERT(leftover >= overhead);

    // Great, we found a region.  We may have to split the available region into
    // up to 2 sub regions depedning on where the aligned allocation lies in the
    // region.  Figure out how much splitting we need to do and attempt to
    // allocate the bookkeeping.
    bool split_before = base != source->base;
    bool split_after  = overhead < leftover;

    if (!split_before && !split_after) {
        // If no splits are required, then this should be easy.  Take the region
        // out of the avail bookkeeping, add it to the allocated bookkeeping and
        // we are finished.
        Region* region = avail_regions_by_size_.erase(source);
        avail_regions_by_base_.erase(*region);
        allocated_regions_by_base_.insert(region);
        out_region.reset(region);
    } else if (!split_before) {
        // If we only have to split after, then this region is aligned with what
        // we want to allocate, but we will not use all of it.  Break it into
        // two pieces and return the one which comes first.
        Region* before_region = region_pool_->AllocRegion(this);
        if (before_region == nullptr)
            return ERR_NO_MEMORY;

        Region* after_region = avail_regions_by_size_.erase(source);

        before_region->base = after_region->base;
        before_region->size = size;
        after_region->base += size;
        after_region->size -= size;

        avail_regions_by_size_.insert(after_region);
        allocated_regions_by_base_.insert(before_region);

        out_region.reset(before_region);
    } else if (!split_after) {
        // If we only have to split before, then this region is not aligned
        // properly with what we want to allocate, but we will use the entire
        // region (after aligning).  Break it into two pieces and return the one
        // which comes after.
        Region* after_region = region_pool_->AllocRegion(this);
        if (after_region == nullptr)
            return ERR_NO_MEMORY;

        Region* before_region = avail_regions_by_size_.erase(source);

        after_region->base   = base;
        after_region->size   = size;
        before_region->size -= size;

        avail_regions_by_size_.insert(before_region);
        allocated_regions_by_base_.insert(after_region);

        out_region.reset(after_region);
    } else {
        // Looks like we need to break our region into 3 chunk and return the
        // middle chunk.  Start by grabbing the bookkeeping we require first.
        Region* region = region_pool_->AllocRegion(this);
        if (region == nullptr)
            return ERR_NO_MEMORY;

        Region* after_region = region_pool_->AllocRegion(this);
        if (after_region == nullptr) {
            region_pool_->FreeRegion(region);
            return ERR_NO_MEMORY;
        }

        Region* before_region = avail_regions_by_size_.erase(source);

        region->base        = before_region->base + overhead;
        region->size        = size;
        after_region->base  = region->base + region->size;
        after_region->size  = before_region->size - size - overhead;
        before_region->size = overhead;

        avail_regions_by_size_.insert(before_region);
        avail_regions_by_size_.insert(after_region);
        avail_regions_by_base_.insert(after_region);
        allocated_regions_by_base_.insert(region);

        out_region.reset(region);
    }
    return NO_ERROR;
}

void RegionAllocator::AddRegionToAvail(Region* region, bool allow_overlap) {
    // Sanity checks.  This region should not exist in any bookkeeping, and
    // should not overlap with any of the regions we are currently tracking.
    DEBUG_ASSERT(!region->ns_tree_sort_by_base_.InContainer());
    DEBUG_ASSERT(!region->ns_tree_sort_by_size_.InContainer());
    DEBUG_ASSERT(!Intersects(allocated_regions_by_base_, *region));
    DEBUG_ASSERT(allow_overlap || !Intersects(avail_regions_by_base_, *region));

    // Find the region which comes before us and the region which comes after us
    // in the tree.
    auto before = avail_regions_by_base_.upper_bound(region->base);
    auto after  = before--;

    // Merge with the region which comes before us if we can.
    uint64_t region_end = (region->base + region->size);    // exclusive end
    if (before.IsValid()) {
        DEBUG_ASSERT(before->base <= region->base);

        uint64_t before_end = (before->base + before->size);    // exclusive end
        if (allow_overlap ? (before_end >= region->base) : (before_end == region->base)) {
            region_end   = mxtl::max(region_end, before_end);
            region->base = before->base;

            auto removed = avail_regions_by_base_.erase(before);
            avail_regions_by_size_.erase(*removed);
            region_pool_->FreeRegion(removed);
        }
    }

    // Merge with the region which comes after us if we can, keep merging if we
    // allow overlaps.
    while (after.IsValid()) {
        DEBUG_ASSERT(region->base < after->base);

        if (!(allow_overlap ? (region_end >= after->base) : (region_end == after->base)))
            break;

        uint64_t after_end = (after->base + after->size);
        region_end = mxtl::max(region_end, after_end);

        auto remove_me = after++;
        auto removed  = avail_regions_by_base_.erase(remove_me);
        avail_regions_by_size_.erase(*removed);
        region_pool_->FreeRegion(removed);

        if (!allow_overlap)
            break;

    }

    // Update the region's size to reflect any mergers which may have taken
    // place, then add the region to the two indexes.
    region->size = region_end - region->base;
    avail_regions_by_base_.insert(region);
    avail_regions_by_size_.insert(region);
}

bool RegionAllocator::Intersects(const Region::WAVLTreeSortByBase& tree,
                                 const ralloc_region_t& region) {
    // Find the first entry in the tree whose base is >= region.base.  If this
    // element exists, and its base is < the exclusive end of region, then
    // we have an intersection.
    auto iter = tree.lower_bound(region.base);
    if (iter.IsValid() && (iter->base < (region.base + region.size)))
        return true;

    // Check the element before us in the tree.  If it exists, we know that it's
    // base is < region.base.  If it's exclusive end is >= region.base, then we
    // have an intersection.
    --iter;
    if (iter.IsValid() && (region.base < (iter->base + iter->size)))
        return true;

    return false;
}
