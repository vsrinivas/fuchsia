// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
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
// makes use of mxtl managed pointer types in order to simplify lifecycle
// management.  RegionPools are managed with mxtl::RefPtr while Regions are
// handed out via mxtl::unique_ptr.  RegionAllocators themselves impose no
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
// The RegionAllocator depends only on malloc/calloc/free and mxtl.  The mxtl
// dependency is not visible to users of the C API.  new/delete implementations
// are provided internally, no global new/delete behavior needs to be defined by
// the user.
//
// == Thread Safety ==
// RegionAllocators do not address issues of thread safety.  Care must be taken
// to ensure that the interactions between Allocators/Pools/Regions are properly
// serialized when operating in multithreaded environments.  In particular, be
// careful about when Region unique_ptrs go out of scope as they will
// automatically be returned to their RegionAllocator as a side effect.
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
//  DEBUG_ASSERT(r3 != nullptr);
//  printf("R3 base %llx size %llx\n", r3->base, r3->size)
//
//  DEBUG_ASSERT(r8 != nullptr);
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
typedef struct ralloc_pool {
// Deliberate empty structure.
//
// Region pools are meant to be opaque structures as far as users are
// concerned.  This empty structure is a pathetic attempt to get some small
// amount of type safety from our compiler (instead of just using void*s
// everywhere)
} ralloc_pool_t;

// C Version of RegionAllocator
typedef struct ralloc_allocator { } ralloc_allocator_t; // Deliberate empty structure.

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
mx_status_t ralloc_create_pool(size_t slab_size, size_t max_memory, ralloc_pool_t** out_pool);
void        ralloc_release_pool(ralloc_pool_t* pool);

// RegionAllocator interface.  Valid operations are...
//
// ++ Create
// ++ SetRegionPool
// ++ Reset (destroys all regions which are available for allocation and returns them to the pool)
// ++ Destroy
// ++ AddRegion (adds a region to the set of regions available for allocation)
// ++ GetBySize (allocates a region based on size/alignment requirements)
// ++ GetSpecific (allocates a region based on specific base/size requirements)
//
mx_status_t ralloc_create_allocator(ralloc_allocator_t** out_allocator);
mx_status_t ralloc_set_region_pool(ralloc_allocator_t* allocator, ralloc_pool_t* pool);
void ralloc_reset_allocator(ralloc_allocator_t* allocator);
void ralloc_destroy_allocator(ralloc_allocator_t* allocator);
mx_status_t ralloc_add_region(ralloc_allocator_t* allocator, const ralloc_region_t* region);

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

// RegionAllocator::Region interface.  In addition to the base/size members
// which may be used to determine the location of the allocation,  valid
// operations are...
//
// Put (return an allocated region to its allocator).
//
void ralloc_put_region(const ralloc_region_t* region);

__END_CDECLS

#ifdef __cplusplus
#include <mxtl/intrusive_single_list.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

// C++ API
class RegionAllocator {
public:
    class RegionPool;

    class Region : public ralloc_region_t {
    private:
        struct ReturnToAllocatorTraits;
    public:
        using UPtr = mxtl::unique_ptr<const Region, struct ReturnToAllocatorTraits>;

    private:
        using WAVLTreeNodeState   = mxtl::WAVLTreeNodeState<Region*>;
        using KeyTraitsSortByBase = mxtl::DefaultKeyedObjectTraits<uint64_t, Region>;

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

        using WAVLTreeSortByBase = mxtl::WAVLTree<uint64_t, Region*,
                                                  KeyTraitsSortByBase,
                                                  WAVLTreeNodeTraitsSortByBase>;
        using WAVLTreeSortBySize = mxtl::WAVLTree<ralloc_region_t, Region*,
                                                  KeyTraitsSortBySize,
                                                  WAVLTreeNodeTraitsSortBySize>;
        using FreeListNodeState = mxtl::SinglyLinkedListNodeState<Region*>;

        struct ReturnToAllocatorTraits {
            inline void operator()(const Region* ptr) const {
                Region* thiz = const_cast<Region*>(ptr);
                DEBUG_ASSERT(thiz->owner_ != nullptr);
                thiz->owner_->ReleaseRegion(thiz);
            }
        };

        // Used by SortByBase key traits
        uint64_t GetKey() const { return base; }

        // So many friends!  I'm the most popular class in the build!!
        friend class  RegionAllocator;
        friend class  RegionPool;
        friend        KeyTraitsSortByBase;
        friend struct KeyTraitsSortBySize;
        friend struct WAVLTreeNodeTraitsSortByBase;
        friend struct WAVLTreeNodeTraitsSortBySize;
        friend struct ReturnToAllocatorTraits;

        // Regions can only be placement new'ed by RegionPool::Slabs.  They
        // cannot be copied, assigned, or deleted.  Externally, they should only
        // be handled by their unique_ptr<>s.
        Region() { }
        Region(const Region& c) = delete;
        Region& operator=(const Region& c) = delete;
        ~Region() = delete;
        void* operator new(size_t, void* p) { return p; }
        void  operator delete(void*) { }

        RegionAllocator* owner_ = nullptr;

        WAVLTreeNodeState ns_tree_sort_by_base_;
        WAVLTreeNodeState ns_tree_sort_by_size_;
    };

    class RegionPool : public mxtl::RefCounted<RegionPool> {
    public:
        using RefPtr = mxtl::RefPtr<RegionPool>;

        RegionPool(const RegionPool& c) = delete;
        RegionPool& operator=(const RegionPool& c) = delete;
        ~RegionPool();

        void* operator new(size_t size) noexcept { return ::malloc(size); }
        void  operator delete(void* obj) { ::free(obj); }

        Region* AllocRegion(RegionAllocator* owner);
        void    FreeRegion(Region* region);

        static RefPtr Create(size_t slab_size, size_t max_memory);

    private:
        struct Slab {
            using UPtr = mxtl::unique_ptr<Slab>;

            void* operator new(size_t, size_t real_size) noexcept { return ::calloc(1, real_size); }
            void  operator delete(void* obj) { ::free(obj); }

            mxtl::SinglyLinkedListNodeState<UPtr> sll_node_state_;
            uint8_t mem_[] __ALIGNED(sizeof(void*));
        };

        struct SlabEntry {
            mxtl::SinglyLinkedListNodeState<SlabEntry*> sll_node_state_;
        };

        static_assert(sizeof(SlabEntry) <= sizeof(Region),
                      "SlabEntries cannot be larger than Regions");

        RegionPool(size_t slab_size, size_t max_memory)
            : slab_size_(slab_size),
              max_memory_(max_memory) { }

        void Grow();

        mxtl::SinglyLinkedList<Slab::UPtr> slabs_;
        mxtl::SinglyLinkedList<SlabEntry*> free_regions_;
        size_t alloc_size_ = 0;
        const size_t slab_size_;
        const size_t max_memory_;
#if LK_DEBUGLEVEL > 1
        size_t in_flight_allocations_ = 0;
#endif
    };

    RegionAllocator() { }
    explicit RegionAllocator(const RegionPool::RefPtr& region_pool)
        : region_pool_(region_pool) { }
    explicit RegionAllocator(RegionPool::RefPtr&& region_pool)
        : region_pool_(mxtl::move(region_pool)) { }
    RegionAllocator(const RegionAllocator& c) = delete;
    RegionAllocator& operator=(const RegionAllocator& c) = delete;

    ~RegionAllocator();

    // Placement new for C API
    void* operator new(size_t, void* mem) { return mem; }

    // Set the RegionPool this RegionAllocator will obtain bookkeeping structures from.
    //
    // Possible return values
    // ++ ERR_BAD_STATE : The RegionAllocator currently has a RegionPool
    // assigned and currently has allocations from this pool.
    mx_status_t SetRegionPool(const RegionPool::RefPtr& region_pool);
    mx_status_t SetRegionPool(RegionPool::RefPtr&& region_pool) {
        RegionPool::RefPtr ref(mxtl::move(region_pool));
        return SetRegionPool(ref);
    }

    // Reset allocator.  Returns all available regions back to the RegionPool.
    // Has no effect on currently allocated regions.
    void Reset();

    // Add a region to the set of allocatable regions.
    //
    // Possible return values
    // ++ ERR_BAD_STATE : Allocator has no RegionPool assigned.
    // ++ ERR_NO_MEMORY : not enough bookkeeping memory available in our
    // assigned region pool to add the region.
    // ++ ERR_INVALID_ARGS : the region being added collides with a previously
    // added region.
    mx_status_t AddRegion(const ralloc_region_t& region);

    // Get a region out of the set of currently available regions which has a
    // specified size and alignment.  Note; the alignment must be a power of
    // two.  Pass 1 if alignment does not matter.
    //
    // Possible return values
    // ++ ERR_BAD_STATE : Allocator has no RegionPool assigned.
    // ++ ERR_NO_MEMORY : not enough bookkeeping memory available in our
    // assigned region pool to perform the allocation.
    // ++ ERR_INVALID_ARGS : size is zero, or alignment is not a power of two.
    // ++ ERR_NOT_FOUND : No suitable region could be found in the set of
    // currently available regions which can satisfy the request.
    mx_status_t GetRegion(uint64_t size, uint64_t alignment, Region::UPtr& out_region);

    // Get a region with a specific location and size out of the set of
    // currently available regions.
    //
    // Possible return values
    // ++ ERR_BAD_STATE : Allocator has no RegionPool assigned.
    // ++ ERR_NO_MEMORY : not enough bookkeeping memory available in our
    // assigned region pool to perform the allocation.
    // ++ ERR_INVALID_ARGS : The size of the requested region is zero.
    // ++ ERR_NOT_FOUND : No suitable region could be found in the set of
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

private:
    friend class Region::ReturnToAllocatorTraits;

    void ReleaseRegion(Region* region);
    void AddRegionToAvail(Region* region);

    mx_status_t AllocFromAvail(Region::WAVLTreeSortBySize::iterator source,
                               Region::UPtr& out_region,
                               uint64_t base,
                               uint64_t size);

    static bool Intersects(const Region::WAVLTreeSortByBase& tree,
                           const ralloc_region_t& region);

    Region::WAVLTreeSortByBase allocated_regions_by_base_;
    Region::WAVLTreeSortByBase avail_regions_by_base_;
    Region::WAVLTreeSortBySize avail_regions_by_size_;
    RegionPool::RefPtr region_pool_;
};

#endif  // ifdef __cplusplus

