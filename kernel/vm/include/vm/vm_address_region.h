// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <magenta/thread_annotations.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <stdint.h>
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
// Allow VmMappings to be created inside the new region with the SPECIFIC flag.
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

#define VMAR_CAN_RWX_FLAGS (VMAR_FLAG_CAN_MAP_READ |  \
                            VMAR_FLAG_CAN_MAP_WRITE | \
                            VMAR_FLAG_CAN_MAP_EXECUTE)

class VmAspace;

// forward declarations
class VmAddressRegion;
class VmMapping;

// Interface for walking a VmAspace-rooted VmAddressRegion/VmMapping tree.
// Override this class and pass an instance to VmAspace::EnumerateChildren().
class VmEnumerator {
public:
    // VmAspace::EnumerateChildren() will call the On* methods in depth-first
    // pre-order. If any call returns false, the traversal will stop. The root
    // VmAspace's lock will be held during the entire traversal.
    // |depth| will be 0 for the root VmAddressRegion.
    virtual bool OnVmAddressRegion(const VmAddressRegion* vmar, uint depth) {
        return true;
    }

    // |vmar| is the parent of |map|.
    virtual bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar,
                             uint depth) {
        return true;
    }

protected:
    VmEnumerator() = default;
    ~VmEnumerator() = default;
};

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
class VmAddressRegionOrMapping : public fbl::RefCounted<VmAddressRegionOrMapping> {
public:
    // If a VMO-mapping, unmap all pages and remove dependency on vm object it has a ref to.
    // Otherwise recursively destroy child VMARs and transition to the DEAD state.
    //
    // Returns MX_OK on success, MX_ERR_BAD_STATE if already dead, and other
    // values on error (typically unmap failure).
    virtual status_t Destroy();

    // accessors
    vaddr_t base() const { return base_; }
    size_t size() const { return size_; }
    uint32_t flags() const { return flags_; }
    const fbl::RefPtr<VmAspace>& aspace() const { return aspace_; }

    // Recursively compute the number of allocated pages within this region
    virtual size_t AllocatedPages() const;

    // Subtype information and safe down-casting
    virtual bool is_mapping() const = 0;
    fbl::RefPtr<VmAddressRegion> as_vm_address_region();
    fbl::RefPtr<VmMapping> as_vm_mapping();

    // Page fault in an address within the region.  Recursively traverses
    // the regions to find the target mapping, if it exists.
    virtual status_t PageFault(vaddr_t va, uint pf_flags) = 0;

    // WAVL tree key function
    vaddr_t GetKey() const { return base(); }

    // Dump debug info
    virtual void Dump(uint depth, bool verbose) const = 0;

private:
    fbl::Canary<fbl::magic("VMRM")> canary_;

protected:
    // friend VmAddressRegion so it can access DestroyLocked
    friend VmAddressRegion;

    // destructor, should only be invoked from RefPtr
    virtual ~VmAddressRegionOrMapping();
    friend fbl::RefPtr<VmAddressRegionOrMapping>;

    enum class LifeCycleState {
        // Initial state: if NOT_READY, then do not invoke Destroy() in the
        // destructor
        NOT_READY,
        // Usual state: information is representative of the address space layout
        ALIVE,
        // Object is invalid
        DEAD
    };

    VmAddressRegionOrMapping(vaddr_t base, size_t size, uint32_t flags,
                             VmAspace* aspace, VmAddressRegion* parent);

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

    // Version of Destroy() that does not acquire the aspace lock
    virtual status_t DestroyLocked() = 0;

    // Version of AllocatedPages() that does not acquire the aspace lock
    virtual size_t AllocatedPagesLocked() const = 0;

    // Transition from NOT_READY to READY, and add references to self to related
    // structures.
    virtual void Activate() = 0;

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

    // utility so WAVL tree can find the intrusive node for the child list
    struct WAVLTreeTraits {
        static fbl::WAVLTreeNodeState<fbl::RefPtr<VmAddressRegionOrMapping>, bool>& node_state(VmAddressRegionOrMapping& obj) {
            return obj.subregion_list_node_;
        }
    };

    // node for element in list of parent's children.
    fbl::WAVLTreeNodeState<fbl::RefPtr<VmAddressRegionOrMapping>, bool> subregion_list_node_;
};

// A representation of a contiguous range of virtual address space
class VmAddressRegion : public VmAddressRegionOrMapping {
public:
    // Create a root region.  This will span the entire aspace
    static status_t CreateRoot(VmAspace& aspace, uint32_t vmar_flags,
                               fbl::RefPtr<VmAddressRegion>* out);
    // Create a subregion of this region
    virtual status_t CreateSubVmar(size_t offset, size_t size, uint8_t align_pow2,
                                   uint32_t vmar_flags, const char* name,
                                   fbl::RefPtr<VmAddressRegion>* out);
    // Create a VmMapping within this region
    virtual status_t CreateVmMapping(size_t mapping_offset, size_t size, uint8_t align_pow2,
                                     uint32_t vmar_flags,
                                     fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
                                     uint arch_mmu_flags, const char* name,
                                     fbl::RefPtr<VmMapping>* out);

    // Find the child region that contains the given addr.  If addr is in a gap,
    // returns nullptr.  This is a non-recursive search.
    virtual fbl::RefPtr<VmAddressRegionOrMapping> FindRegion(vaddr_t addr);

    // Unmap a subset of the region of memory in the containing address space,
    // returning it to this region to allocate.  If a subregion is entirely in
    // the range, that subregion is destroyed.  If a subregion is partially in
    // the range, Unmap() will fail.
    virtual status_t Unmap(vaddr_t base, size_t size);

    // Change protections on a subset of the region of memory in the containing
    // address space.  If the requested range overlaps with a subregion,
    // Protect() will fail.
    virtual status_t Protect(vaddr_t base, size_t size, uint new_arch_mmu_flags);

    const char* name() const { return name_; }
    bool is_mapping() const override { return false; }

    void Dump(uint depth, bool verbose) const override;
    status_t PageFault(vaddr_t va, uint pf_flags) override;

protected:
    // constructor for use in creating a VmAddressRegionDummy
    explicit VmAddressRegion();

    friend class VmAspace;
    // constructor for use in creating the kernel aspace singleton
    explicit VmAddressRegion(VmAspace& kernel_aspace);
    // Count the allocated pages, caller must be holding the aspace lock
    size_t AllocatedPagesLocked() const override;
    // Used to implement VmAspace::EnumerateChildren.
    // |aspace_->lock()| must be held.
    virtual bool EnumerateChildrenLocked(VmEnumerator* ve, uint depth);

    friend class VmMapping;
    // Remove *region* from the subregion list
    void RemoveSubregion(VmAddressRegionOrMapping* region);

    friend fbl::RefPtr<VmAddressRegion>;

private:
    using ChildList = fbl::WAVLTree<vaddr_t, fbl::RefPtr<VmAddressRegionOrMapping>,
                                     fbl::DefaultKeyedObjectTraits<vaddr_t, VmAddressRegionOrMapping>,
                                     WAVLTreeTraits>;

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmAddressRegion);

    fbl::Canary<fbl::magic("VMAR")> canary_;

    // private constructors, use Create...() instead
    VmAddressRegion(VmAspace& aspace, vaddr_t base, size_t size, uint32_t vmar_flags);
    VmAddressRegion(VmAddressRegion& parent, vaddr_t base, size_t size, uint32_t vmar_flags, const char* name);

    // Version of FindRegion() that does not acquire the aspace lock
    fbl::RefPtr<VmAddressRegionOrMapping> FindRegionLocked(vaddr_t addr);

    // Version of Destroy() that does not acquire the aspace lock
    status_t DestroyLocked() override;

    void Activate() override;

    // Helper to share code between CreateSubVmar and CreateVmMapping
    status_t CreateSubVmarInternal(size_t offset, size_t size, uint8_t align_pow2,
                                   uint32_t vmar_flags,
                                   fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
                                   uint arch_mmu_flags, const char* name,
                                   fbl::RefPtr<VmAddressRegionOrMapping>* out);

    // Create a new VmMapping within this region, overwriting any existing
    // mappings that are in the way.  If the range crosses a subregion, the call
    // fails.
    status_t OverwriteVmMapping(vaddr_t base, size_t size, uint32_t vmar_flags,
                                fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
                                uint arch_mmu_flags,
                                fbl::RefPtr<VmAddressRegionOrMapping>* out);

    // Implementation for Unmap() and OverwriteVmMapping() that does not hold
    // the aspace lock.
    status_t UnmapInternalLocked(vaddr_t base, size_t size, bool can_destroy_regions);

    // internal utilities for interacting with the children list

    // returns true if it would be valid to create a child in the
    // range [base, base+size)
    bool IsRangeAvailableLocked(vaddr_t base, size_t size);

    // returns true if we can meet the allocation between the given children,
    // and if so populates pva with the base address to use.
    bool CheckGapLocked(const ChildList::iterator& prev, const ChildList::iterator& next,
                        vaddr_t* pva, vaddr_t search_base, vaddr_t align,
                        size_t region_size, size_t min_gap, uint arch_mmu_flags);

    // search for a spot to allocate for a region of a given size
    status_t AllocSpotLocked(size_t size, uint8_t align_pow2, uint arch_mmu_flags, vaddr_t* spot);

    // Allocators
    status_t LinearRegionAllocatorLocked(size_t size, uint8_t align_pow2, uint arch_mmu_flags,
                                         vaddr_t* spot);
    status_t NonCompactRandomizedRegionAllocatorLocked(size_t size, uint8_t align_pow2,
                                                       uint arch_mmu_flags, vaddr_t* spot);
    status_t CompactRandomizedRegionAllocatorLocked(size_t size, uint8_t align_pow2,
                                                    uint arch_mmu_flags, vaddr_t* spot);

    // Utility for allocators for iterating over gaps between allocations
    // F should have a signature of bool func(vaddr_t gap_base, size_t gap_size).
    // If func returns false, the iteration stops.  gap_base will be aligned in
    // accordance with align_pow2.
    template <typename F>
    void ForEachGap(F func, uint8_t align_pow2);

    // list of subregions, indexed by base address
    ChildList subregions_;

    const char name_[32] = {};
};

// A VmAddressRegion that always returns errors.  This is used to break a
// reference cycle between root VMARs and VmAspaces.
class VmAddressRegionDummy final : public VmAddressRegion {
public:
    VmAddressRegionDummy()
        : VmAddressRegion() {}

    status_t CreateSubVmar(size_t offset, size_t size, uint8_t align_pow2,
                           uint32_t vmar_flags, const char* name,
                           fbl::RefPtr<VmAddressRegion>* out) override {
        return MX_ERR_BAD_STATE;
    }
    status_t CreateVmMapping(size_t mapping_offset, size_t size, uint8_t align_pow2,
                             uint32_t vmar_flags,
                             fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
                             uint arch_mmu_flags, const char* name,
                             fbl::RefPtr<VmMapping>* out) override {
        return MX_ERR_BAD_STATE;
    }

    fbl::RefPtr<VmAddressRegionOrMapping> FindRegion(vaddr_t addr) override {
        return nullptr;
    }

    status_t Protect(vaddr_t base, size_t size, uint new_arch_mmu_flags) override {
        return MX_ERR_BAD_STATE;
    }

    status_t Unmap(vaddr_t base, size_t size) override {
        return MX_ERR_BAD_STATE;
    }

    void Dump(uint depth, bool verbose) const override {
        return;
    }

    status_t PageFault(vaddr_t va, uint pf_flags) override {
        // We should never be trying to page fault on this...
        ASSERT(false);
        return MX_ERR_BAD_STATE;
    }

    size_t AllocatedPages() const override {
        return 0;
    }

    status_t Destroy() override {
        return MX_ERR_BAD_STATE;
    }

    ~VmAddressRegionDummy() override {}

    size_t AllocatedPagesLocked() const override {
        return 0;
    }

    status_t DestroyLocked() override {
        return MX_ERR_BAD_STATE;
    }

    void Activate() override {
        return;
    }

    bool EnumerateChildrenLocked(VmEnumerator* ve, uint depth) override {
        return false;
    }
};

// A representation of the mapping of a VMO into the address space
class VmMapping final : public VmAddressRegionOrMapping,
                        public fbl::DoublyLinkedListable<VmMapping *> {
public:
    // Accessors for VMO-mapping state
    uint arch_mmu_flags() const { return arch_mmu_flags_; }
    uint64_t object_offset() const { return object_offset_; }
    fbl::RefPtr<VmObject> vmo() const { return object_; };

    // Convenience wrapper for vmo()->DecommitRange() with the necessary
    // offset modification and locking.
    status_t DecommitRange(size_t offset, size_t len, size_t* decommitted);

    // Map in pages from the underlying vm object, optionally committing pages as it goes
    status_t MapRange(size_t offset, size_t len, bool commit);

    // Unmap a subset of the region of memory in the containing address space,
    // returning it to the parent region to allocate.  If all of the memory is unmapped,
    // Destroy()s this mapping.  If a subrange of the mapping is specified, the
    // mapping may be split.
    status_t Unmap(vaddr_t base, size_t size);

    // Change access permissions for this mapping.  It is an error to specify a
    // caching mode in the flags.  This will persist the caching mode the
    // mapping was created with.  If a subrange of the mapping is specified, the
    // mapping may be split.
    status_t Protect(vaddr_t base, size_t size, uint new_arch_mmu_flags);

    bool is_mapping() const override { return true; }

    void Dump(uint depth, bool verbose) const override;
    status_t PageFault(vaddr_t va, uint pf_flags) override;

protected:
    ~VmMapping() override;
    friend fbl::RefPtr<VmMapping>;

    // private apis from VmObject land
    friend class VmObject;

    // unmap any pages that map the passed in vmo range. May not intersect with this range
    status_t UnmapVmoRangeLocked(uint64_t start, uint64_t size) const;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VmMapping);

    fbl::Canary<fbl::magic("VMAP")> canary_;

    // allow VmAddressRegion to manipulate VmMapping internals for construction
    // and bookkeeping
    friend class VmAddressRegion;

    // private constructors, use VmAddressRegion::Create...() instead
    VmMapping(VmAddressRegion& parent, vaddr_t base, size_t size, uint32_t vmar_flags,
              fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset, uint arch_mmu_flags);

    // Version of Destroy() that does not acquire the aspace lock
    status_t DestroyLocked() override;

    // Implementation for Unmap().  This does not acquire the aspace lock, and
    // supports partial unmapping.
    status_t UnmapLocked(vaddr_t base, size_t size);

    // Implementation for Protect().  This does not acquire the aspace lock.
    status_t ProtectLocked(vaddr_t base, size_t size, uint new_arch_mmu_flags);

    // Version of AllocatedPages() that does not acquire the aspace lock
    size_t AllocatedPagesLocked() const override;

    void Activate() override;

    // Version of Activate that does not take the object_ lock.
    // Should be annotated TA_REQ(object_->lock()), but due to limitations
    // in Clang around capability aliasing, we need to relax the analysis.
    void ActivateLocked();

    // pointer and region of the object we are mapping
    fbl::RefPtr<VmObject> object_;
    uint64_t object_offset_ = 0;

    // cached mapping flags (read/write/user/etc)
    uint arch_mmu_flags_;

    // used to detect recursions through the vmo fault path
    bool currently_faulting_ = false;
};
