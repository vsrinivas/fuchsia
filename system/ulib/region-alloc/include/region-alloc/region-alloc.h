// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <stdbool.h>
#include <stddef.h>

// RegionAllocator
//
// == Overview ==
// A RegionAllocator is a utility class designed to help with the bookkeeping
// involved in managing the allocation/partitioning of a 64-bit space into
// non-overlapping "Regions".  In addition to the RegionAllocator, there are two
// other classes involved in the use of a RegionAllcator;
// RegionAllocator::Region and RegionAllocator::RegionPool.
//
// A Region consists of an unsigned 64-bit base address and an unsigned 64-bit
// size.  A Region is considered valid iff its size is non-zero, and it does not
// wrap its 64-bit space.
//
// See the "Memory Allocation" section for a discussion of the RegionPool.
//
// RegionAllocator users can create an allocator and then add any number of
// non-overlapping Regions to its pool of regions available for allocation.
// They may then request that regions be allocated from the pool either by
// requesting that a region be allocated with a particular size/alignment, or
// by asking for a specific base/size.  The RegionAllocator will manage all of
// the bookkeeping involved in breaking available regions into smaller chunks,
// tracking allocated regions, and re-merging regions when they are returned to
// the allocator.
//
// == Memory Allocaion ==
// RegionAllocators require dynamically allocated memory in order to store the
// bookkeeping required for managing available regions.  In order to control
// heap fragmentation and the frequency of heap interaction, A RegionPool object
// is used to allocate bookkeeping overhead in larger slabs which are carved up
// and placed on a free list to be used by a RegionAllocator.  RegionPools are
// created with a defined slab size as well as a maximum memory limit.  The pool
// will initially allocate a single slab, but will attempt to grow any time
// bookkeeping is needed but the free list is empty and the allocation of
// another slab would not push the allocator over its maximum memory limit.
//
// RegionPools are ref-counted objects that may be shared by multiple
// RegionAllocators.  This allows sub-systems which use multiple allocators to
// impose system-wide limits on bookkeeping overhead.  A RegionPool must be
// assigned to a RegionAllocator before it can be used, and the pool may not be
// re-assigned while the allocator is using any bookkeeping from the pool.
//
// == APIs and Object lifecycle management ==
// Both C and C++ APIs are provided for using the RegionAllocator.  The C++ API
// makes use of fbl managed pointer types in order to simplify lifecycle
// management.  RegionPools are managed with fbl::RefPtr while Regions are
// handed out via fbl::unique_ptr.  RegionAllocators themselves impose no
// lifecycle restrictions and may be heap allocated, stack allocated, or
// embedded directly in objects as the user sees fit.  It is an error to allow a
// RegionAllocator to destruct while there are allocations in flight.
//
// The C API is a wrapper over the C++ API.  Because of this, automatic
// lifecycle management is lost.  Users must take care to manually return their
// allocated Regions to their RegionAllocator, to manually destroy their
// RegionAllocators, and to manually release their reference on their RegionPool
// when they are finished using them.  In addition, because the C compiler
// cannot know the size of a RegionAllocator object, it is not possible to
// either stack allocate or embed a RegionAllocator via the C API.  Dynamic
// allocation is the only option.
//
// == Dependencies ==
// The RegionAllocator depends only on malloc/free and fbl.  The fbl
// dependency is not visible to users of the C API.  new/delete implementations
// are provided internally, no global new/delete behavior needs to be defined by
// the user.
//
// == Thread Safety ==
// RegionAllocator and RegionPools use fbl::Mutex objects to provide thread
// safety in multi-threaded environments.  As such, RegionAllocators are not
// currently suitable for use in code which may run at IRQ context, or which
// must never block.
//
// Each RegionAllocator has its own mutex allowing for concurrent access across
// multiple allocators, even when the allocators share the same RegionPool.
// RegionPools also hold their own mutex which may be obtained by an Allocator
// while holding the Allocator's Mutex.
//
// == Simple Usage Example ==
//
// /* Create a pool and assign it to a stack allocated allocator.  Limit the
//  * bookkeeping memory to 32KB allocated from a single slab.  This will ensure
//  * that no heap interactions take place after startup (during operation).
//  */
//  RegionAllocator alloc(
//      RegionAllocator::RegionPool::Create(32 << 10, 32 << 10));
//
//  /* Add regions to the pool which can be allocated from */
//  alloc.AddRegion({ .base = 0xC0000000, .  size = 0x40000000 });  // [3GB,   4GB)
//  alloc.AddRegion({ .base = 0x4000000000, .size = 0x40000000 });  // [256GB, 257GB)
//
//  /* Grab some specific regions out of the available regions.
//   * Note: auto here will expand to RegionAllocator::Region::UPtr.  Feel free
//   * to add your own using statement to lessen the C++ naming pain.
//   */
//  auto r1 = alloc.GetRegion({ 0xC0100000,   0x100000 });  // [3GB + 1MB,   3GB + 2MB)
//  auto r2 = alloc.GetRegion({ 0x4000100000, 0x100000 });  // [256GB + 1MB, 256GB + 2MB)
//
//  /* Grab some pointer aligned regions of various sizes */
//  auto r3 = alloc.GetRegion(1024);
//  auto r4 = alloc.GetRegion(75);
//  auto r5 = alloc.GetRegion(80000);
//
//  /* Grab some page aligned regions of various sizes */
//  auto r6 = alloc.GetRegion(1024,  4 << 10);
//  auto r7 = alloc.GetRegion(75,    4 << 10);
//  auto r8 = alloc.GetRegion(80000, 4 << 10);
//
//  /* Print some stuff about some of the allocations */
//  MX_DEBUG_ASSERT(r3 != nullptr);
//  printf("R3 base %llx size %llx\n", r3->base, r3->size)
//
//  MX_DEBUG_ASSERT(r8 != nullptr);
//  printf("R8 base %llx size %llx\n", r8->base, r8->size)
//
//  /* No need to clean up.  Regions will automatically be returned to the
//   * allocator as they go out of scope.  Then the allocator will return all of
//   * its available regions to the pool when it goes out of scope.  Finally, the
//   * pool will free all of its memory as the allocator releases it reference
//   * to the pool.
//   */
//
__BEGIN_CDECLS

// C API

// C Version of RegionAllocator::RegionPool
// This type is opaque to users; 'struct ralloc_pool' is not actually
// defined anywhere, but this provides a distinct type for pointers.
typedef struct ralloc_pool ralloc_pool_t;

// C Version of RegionAllocator

// This type is opaque to users; 'struct ralloc_allocator' is not actually
// defined anywhere, but this provides a distinct type for pointers.
typedef struct ralloc_allocator ralloc_allocator_t;

// C Version of RegionAllocator::Region
typedef struct ralloc_region {
    uint64_t base;
    uint64_t size;
} ralloc_region_t;

// RegionAllocator::RegionPool interface.  Valid operations are...
//
// ++ Create  (specific memory limits at the time of creation)
// ++ Release (release the C reference to the object.
//
#define REGION_POOL_SLAB_SIZE (4u << 10)
mx_status_t ralloc_create_pool(size_t max_memory, ralloc_pool_t** out_pool);
void        ralloc_release_pool(ralloc_pool_t* pool);

// RegionAllocator interface.  Valid operations are...
//
// ++ Create
// ++ SetRegionPool
// ++ Reset (destroys all regions which are available for allocation and returns them to the pool)
// ++ Destroy
// ++ AddRegion (adds a region to the set of regions available for allocation)
// ++ SubtractRegion (subtracts a region from the set of regions available for allocation)
// ++ GetBySize (allocates a region based on size/alignment requirements)
// ++ GetSpecific (allocates a region based on specific base/size requirements)
//
mx_status_t ralloc_create_allocator(ralloc_allocator_t** out_allocator);
mx_status_t ralloc_set_region_pool(ralloc_allocator_t* allocator, ralloc_pool_t* pool);
void ralloc_reset_allocator(ralloc_allocator_t* allocator);
void ralloc_destroy_allocator(ralloc_allocator_t* allocator);
mx_status_t ralloc_add_region(ralloc_allocator_t* allocator,
                              const ralloc_region_t* region,
                              bool allow_overlap);
mx_status_t ralloc_sub_region(ralloc_allocator_t* allocator,
                              const ralloc_region_t* region,
                              bool allow_incomplete);

mx_status_t ralloc_get_sized_region_ex(
        ralloc_allocator_t* allocator,
        uint64_t size,
        uint64_t alignment,
        const ralloc_region_t** out_region);

mx_status_t ralloc_get_specific_region_ex(
        ralloc_allocator_t* allocator,
        const ralloc_region_t* requested_region,
        const ralloc_region_t** out_region);

// Wrapper versions of the _ex functions for those who don't care about the
// specific reason for failure.
static inline const ralloc_region_t* ralloc_get_sized_region(
        ralloc_allocator_t* allocator,
        uint64_t size,
        uint64_t alignment) {
    const ralloc_region_t* ret;
    ralloc_get_sized_region_ex(allocator, size, alignment, &ret);
    return ret;
}

static inline const ralloc_region_t* ralloc_get_specific_region(
        ralloc_allocator_t* allocator,
        const ralloc_region_t* requested_region) {
    const ralloc_region_t* ret;
    ralloc_get_specific_region_ex(allocator, requested_region, &ret);
    return ret;
}

// Report the number of regions which are available for allocation, or which are
// currently allocated.
size_t ralloc_get_allocated_region_count(const ralloc_allocator_t* allocator);
size_t ralloc_get_available_region_count(const ralloc_allocator_t* allocator);

// RegionAllocator::Region interface.  In addition to the base/size members
// which may be used to determine the location of the allocation,  valid
// operations are...
//
// Put (return an allocated region to its allocator).
//
void ralloc_put_region(const ralloc_region_t* region);

__END_CDECLS

#ifdef __cplusplus

#include <fbl/intrusive_single_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>
#include <fbl/unique_ptr.h>

// C++ API
class RegionAllocator {
public:
    class Region;
    using RegionSlabTraits = fbl::ManualDeleteSlabAllocatorTraits<Region*, REGION_POOL_SLAB_SIZE>;

    class Region : public ralloc_region_t,
                   public fbl::SlabAllocated<RegionSlabTraits>,
                   public fbl::Recyclable<Region> {
    public:
        using UPtr = fbl::unique_ptr<const Region>;

    private:
        using WAVLTreeNodeState   = fbl::WAVLTreeNodeState<Region*>;
        using KeyTraitsSortByBase = fbl::DefaultKeyedObjectTraits<uint64_t, Region>;

        struct WAVLTreeNodeTraitsSortByBase {
            static WAVLTreeNodeState& node_state(Region& r) { return r.ns_tree_sort_by_base_; }
        };

        struct WAVLTreeNodeTraitsSortBySize {
            static WAVLTreeNodeState& node_state(Region& r) { return r.ns_tree_sort_by_size_; }
        };

        struct KeyTraitsSortBySize {
            static const ralloc_region_t& GetKey(const Region& r) { return r; }

            static bool LessThan(const ralloc_region_t& k1, const ralloc_region_t& k2) {
                return (k1.size < k2.size) || ((k1.size == k2.size) && (k1.base < k2.base));
            }

            static bool EqualTo(const ralloc_region_t& k1, const ralloc_region_t& k2) {
                return (k1.size == k2.size) && (k1.base == k2.base);
            }
        };

        using WAVLTreeSortByBase = fbl::WAVLTree<uint64_t, Region*,
                                                  KeyTraitsSortByBase,
                                                  WAVLTreeNodeTraitsSortByBase>;
        using WAVLTreeSortBySize = fbl::WAVLTree<ralloc_region_t, Region*,
                                                  KeyTraitsSortBySize,
                                                  WAVLTreeNodeTraitsSortBySize>;

        // Used by SortByBase key traits
        uint64_t GetKey() const { return base; }

        // So many friends!  I'm the most popular class in the build!!
        friend class  RegionAllocator;
        friend class  RegionPool;
        friend class  fbl::unique_ptr<const Region>;
        friend class  fbl::Recyclable<Region>;
        friend        KeyTraitsSortByBase;
        friend struct KeyTraitsSortBySize;
        friend struct WAVLTreeNodeTraitsSortByBase;
        friend struct WAVLTreeNodeTraitsSortBySize;

        // Regions can only be placement new'ed by the RegionPool slab
        // allocator.  They cannot be copied, assigned, or deleted.  Externally,
        // they should only be handled by their unique_ptr<>s.
        explicit Region(RegionAllocator* owner) : owner_(owner) { }
        friend class  fbl::SlabAllocator<RegionSlabTraits>;
        DISALLOW_COPY_ASSIGN_AND_MOVE(Region);

        // When a user's unique_ptr<> reference to this region goes out of
        // scope, we will be "recycled".  Don't actually delete the region.
        // Instead, recycle it back into the set of available regions.  The
        // memory for the bookkeeping will eventually be deleted when it merges
        // with another available region, or when the allocator finally shuts
        // down.
        void fbl_recycle() {
            MX_DEBUG_ASSERT(owner_ != nullptr);
            owner_->ReleaseRegion(this);
        }

        RegionAllocator* owner_;
        WAVLTreeNodeState ns_tree_sort_by_base_;
        WAVLTreeNodeState ns_tree_sort_by_size_;
    };

    class RegionPool : public fbl::RefCounted<RegionPool>,
                       public fbl::SlabAllocator<RegionSlabTraits> {
    public:
        using RefPtr = fbl::RefPtr<RegionPool>;

        static constexpr size_t SLAB_SIZE = RegionSlabTraits::SLAB_SIZE;

        static RefPtr Create(size_t max_memory);

    private:
        // Only our RefPtr's are allowed to destroy us.
        friend fbl::RefPtr<RegionPool>;

        // Attempt to allocate at least one slab up front when we are created.
        explicit RegionPool(size_t num_slabs)
            : fbl::SlabAllocator<RegionSlabTraits>(num_slabs, true) { }

        ~RegionPool() { }

        void* operator new(size_t sz) noexcept { return ::malloc(sz); }
        void  operator delete(void* obj) { ::free(obj); }

        // No one may copy, assign or move us.
        DISALLOW_COPY_ASSIGN_AND_MOVE(RegionPool);
    };

    RegionAllocator() { }
    explicit RegionAllocator(const RegionPool::RefPtr& region_pool)
        : region_pool_(region_pool) { }
    explicit RegionAllocator(RegionPool::RefPtr&& region_pool)
        : region_pool_(fbl::move(region_pool)) { }
    RegionAllocator(const RegionAllocator& c) = delete;
    RegionAllocator& operator=(const RegionAllocator& c) = delete;

    ~RegionAllocator();

    // Set the RegionPool this RegionAllocator will obtain bookkeeping structures from.
    //
    // Possible return values
    // ++ MX_ERR_BAD_STATE : The RegionAllocator currently has a RegionPool
    // assigned and currently has allocations from this pool.
    mx_status_t SetRegionPool(const RegionPool::RefPtr& region_pool);
    mx_status_t SetRegionPool(RegionPool::RefPtr&& region_pool) {
        RegionPool::RefPtr ref(fbl::move(region_pool));
        return SetRegionPool(ref);
    }

    // Reset allocator.  Returns all available regions back to the RegionPool.
    // Has no effect on currently allocated regions.
    void Reset();

    // Add a region to the set of allocatable regions.
    //
    // If allow_overlap is false, the added region may not overlap with any
    // previously added region and will be rejected if it does.  If
    // allow_overlap is true, the added region will be union'ed with existing
    // available regions, provided it does not intersect any currently allocated
    // region.
    //
    // Possible return values
    // ++ MX_ERR_BAD_STATE : Allocator has no RegionPool assigned.
    // ++ MX_ERR_NO_MEMORY : not enough bookkeeping memory available in our
    // assigned region pool to add the region.
    // ++ MX_ERR_INVALID_ARGS : One of the following conditions applies.
    // ++++ The region is invalid (wraps the space, or size is zero)
    // ++++ The region being added intersects one or more currently
    //      allocated regions.
    // ++++ The region being added intersects one ore more of the currently
    //      available regions, and allow_overlap is false.
    mx_status_t AddRegion(const ralloc_region_t& region, bool allow_overlap = false);

    // Subtract a region from the set of allocatable regions.
    //
    // If allow_incomplete is false, the subtracted region must exist entirely
    // within the set of available regions.  If allow_incomplete is true, the
    // subracted region will remove any portion of any availble region it
    // intersects with.
    //
    // Regardless of the value of the allow_incomplete flag, it is illegal to
    // attempt to subtract a region which intersects any currently allocated
    // region.
    //
    // Possible return values
    // ++ MX_ERR_BAD_STATE : Allocator has no RegionPool assigned.
    // ++ MX_ERR_NO_MEMORY : not enough bookkeeping memory available in our
    // assigned region pool to subtract the region.
    // ++ MX_ERR_INVALID_ARGS : One of the following conditions applies.
    // ++++ The region is invalid (wraps the space, or size is zero)
    // ++++ The region being subtracted intersects one or more currently
    //      allocated regions.
    // ++++ The region being subtracted intersects portions of the space which
    //      are absent from both the allocated and available sets, and
    //      allow_incomplete is false.
    mx_status_t SubtractRegion(const ralloc_region_t& region, bool allow_incomplete = false);

    // Get a region out of the set of currently available regions which has a
    // specified size and alignment.  Note; the alignment must be a power of
    // two.  Pass 1 if alignment does not matter.
    //
    // Possible return values
    // ++ MX_ERR_BAD_STATE : Allocator has no RegionPool assigned.
    // ++ MX_ERR_NO_MEMORY : not enough bookkeeping memory available in our
    // assigned region pool to perform the allocation.
    // ++ MX_ERR_INVALID_ARGS : size is zero, or alignment is not a power of two.
    // ++ MX_ERR_NOT_FOUND : No suitable region could be found in the set of
    // currently available regions which can satisfy the request.
    mx_status_t GetRegion(uint64_t size, uint64_t alignment, Region::UPtr& out_region);

    // Get a region with a specific location and size out of the set of
    // currently available regions.
    //
    // Possible return values
    // ++ MX_ERR_BAD_STATE : Allocator has no RegionPool assigned.
    // ++ MX_ERR_NO_MEMORY : not enough bookkeeping memory available in our
    // assigned region pool to perform the allocation.
    // ++ MX_ERR_INVALID_ARGS : The size of the requested region is zero.
    // ++ MX_ERR_NOT_FOUND : No suitable region could be found in the set of
    // currently available regions which can satisfy the request.
    mx_status_t GetRegion(const ralloc_region_t& requested_region, Region::UPtr& out_region);

    // Helper which defaults the alignment of a size/alignment based allocation
    // to pointer-aligned.
    mx_status_t GetRegion(uint64_t size, Region::UPtr& out_region) {
        return GetRegion(size, sizeof(void*), out_region);
    }

    // Helper versions of the GetRegion methods for those who don't care
    // about the specific reason for failure (nullptr will be returned on
    // failure).
    Region::UPtr GetRegion(uint64_t size, uint64_t alignment) {
        Region::UPtr ret;
        GetRegion(size, alignment, ret);
        return ret;
    }

    Region::UPtr GetRegion(uint64_t size) {
        Region::UPtr ret;
        GetRegion(size, ret);
        return ret;
    }

    Region::UPtr GetRegion(const ralloc_region_t& requested_region) {
        Region::UPtr ret;
        GetRegion(requested_region, ret);
        return ret;
    }

    size_t AllocatedRegionCount() const {
        fbl::AutoLock alloc_lock(&alloc_lock_);
        return allocated_regions_by_base_.size();
    }

    size_t AvailableRegionCount() const {
        fbl::AutoLock alloc_lock(&alloc_lock_);
        return avail_regions_by_base_.size();
    }

private:
    mx_status_t AddSubtractSanityCheckLocked(const ralloc_region_t& region);
    void ReleaseRegion(Region* region);
    void AddRegionToAvailLocked(Region* region, bool allow_overlap = false);

    mx_status_t AllocFromAvailLocked(Region::WAVLTreeSortBySize::iterator source,
                                     Region::UPtr& out_region,
                                     uint64_t base,
                                     uint64_t size);

    static bool IntersectsLocked(const Region::WAVLTreeSortByBase& tree,
                                 const ralloc_region_t& region);

    /* Locking notes:
     *
     * alloc_lock_ protects all of the bookkeeping members of the
     * RegionAllocator.  This includes the allocated index, the available
     * indicies (by base and by size) and the region pool.
     *
     * The alloc_lock_ may be held while calling into a RegionAllocator's
     * assigned RegionPool, but code from the RegionPool will never call into
     * the RegionAllocator.
     */
    mutable fbl::Mutex alloc_lock_;
    Region::WAVLTreeSortByBase allocated_regions_by_base_;
    Region::WAVLTreeSortByBase avail_regions_by_base_;
    Region::WAVLTreeSortBySize avail_regions_by_size_;
    RegionPool::RefPtr region_pool_;
};

// If this is C++, clear out this pre-processor constant.  People can get to the
// constant using more C++-ish methods (like RegionAllocator::RegionPool::SLAB_SIZE)
#undef REGION_POOL_SLAB_SIZE

#endif  // ifdef __cplusplus
