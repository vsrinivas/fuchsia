// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_page_list.h>
#include <mxtl/deleter.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <stdint.h>

// Creation flags for VmAddressRegion and VmMappings

// When randomly allocating subregions, reduce sprawl (hint).
// Currently ignored, since randomization is not yet implemented.
// TODO(teisenbe): Remove this comment when randomization is implemented.
#define VMAR_FLAG_COMPACT (1 << 0)
// Request that the new region be at the specified offset in its parent region.
#define VMAR_FLAG_SPECIFIC (1 << 1)
// Allow VmMappings to be created inside the new region with the SPECIFIC flag.
#define VMAR_FLAG_CAN_MAP_SPECIFIC (1 << 2)
// When on a VmAddressRegion, allow VmMappings to be created inside the region
// with read permissions.  When on a VmMapping, controls whether or not the
// mapping can gain this permission.
#define VMAR_FLAG_CAN_MAP_READ (1 << 3)
// When on a VmAddressRegion, allow VmMappings to be created inside the region
// with write permissions.  When on a VmMapping, controls whether or not the
// mapping can gain this permission.
#define VMAR_FLAG_CAN_MAP_WRITE (1 << 4)
// When on a VmAddressRegion, allow VmMappings to be created inside the region
// with execute permissions.  When on a VmMapping, controls whether or not the
// mapping can gain this permission.
#define VMAR_FLAG_CAN_MAP_EXECUTE (1 << 5)

// TODO(teisenbe): Remove this flag once DDK uses VMAR interface
// This flag is a hint to try to create the mapping high up in the address
// space.
#define VMAR_FLAG_MAP_HIGH (1 << 6)

#define VMAR_CAN_RWX_FLAGS (VMAR_FLAG_CAN_MAP_READ | \
                            VMAR_FLAG_CAN_MAP_WRITE | \
                            VMAR_FLAG_CAN_MAP_EXECUTE)

class VmAspace;

// forward declarations
class VmAddressRegion;
class VmMapping;

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
class VmAddressRegionOrMapping : public mxtl::RefCounted<VmAddressRegionOrMapping> {
public:
    // If a VMO-mapping, unmap all pages and remove dependency on vm object it has a ref to.
    // Otherwise recursively destroy child VMARs and transition to the DEAD state.
    //
    // Returns NO_ERROR on success, ERR_BAD_STATE if already dead, and other
    // values on error (typically unmap failure).
    status_t Destroy();

    // accessors
    vaddr_t base() const { return base_; }
    size_t size() const { return size_; }
    uint32_t flags() const { return flags_; }

    // Recursively compute the number of allocated pages within this region
    virtual size_t AllocatedPages() const;

    // Subtype information and safe down-casting
    virtual bool is_mapping() const = 0;
    mxtl::RefPtr<VmAddressRegion> as_vm_address_region();
    mxtl::RefPtr<VmMapping> as_vm_mapping();

    // Page fault in an address within the region.  Recursively traverses
    // the regions to find the target mapping, if it exists.
    virtual status_t PageFault(vaddr_t va, uint pf_flags) = 0;

    // WAVL tree key function
    vaddr_t GetKey() const { return base(); }

    // Dump debug info
    virtual void Dump(uint depth = 0) const = 0;

protected:
    // friend VmAddressRegion so it can access DestroyLocked
    friend VmAddressRegion;

    friend mxtl::default_delete<VmAddressRegionOrMapping>;
    // destructor, should only be invoked from RefPtr
    virtual ~VmAddressRegionOrMapping();

    enum class LifeCycleState {
        // Initial state: if NOT_READY, then do not invoke Destroy() in the
        // destructor
        NOT_READY,
        // Usual state: information is representative of the address space layout
        ALIVE,
        // Object is invalid
        DEAD
    };

    VmAddressRegionOrMapping(uint32_t magic, vaddr_t base, size_t size, uint32_t flags,
                             VmAspace& aspace, VmAddressRegion* parent, const char* name);

    // Check if the given *arch_mmu_flags* are allowed under this
    // regions *flags_*
    bool is_valid_mapping_flags(uint arch_mmu_flags);

    bool is_in_range(vaddr_t base, size_t size) const {
        const size_t offset = base - base_;
        return base >= base_ && offset < size_ && size_ - offset >= size;
    }

    // Version of Destroy() that does not acquire the aspace lock
    virtual status_t DestroyLocked() = 0;

    // Version of AllocatedPages() that does not acquire the aspace lock
    virtual size_t AllocatedPagesLocked() const = 0;

    // Transition from NOT_READY to READY, and add references to self to related
    // structures.
    virtual void Activate() = 0;

    // magic value
    uint32_t magic_;

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
    const mxtl::RefPtr<VmAspace> aspace_;

    // pointer back to our parent region (nullptr if root or destroyed)
    VmAddressRegion* parent_;

    struct WAVLTreeTraits {
        static mxtl::WAVLTreeNodeState<mxtl::RefPtr<VmAddressRegionOrMapping>, bool>& node_state(VmAddressRegionOrMapping& obj) {
            return obj.subregion_list_node_;
        }
    };

    // node for element in list of parent's children.
    mxtl::WAVLTreeNodeState<mxtl::RefPtr<VmAddressRegionOrMapping>, bool> subregion_list_node_;

    char name_[32];
};

// A representation of a contiguous range of virtual address space
class VmAddressRegion final : public VmAddressRegionOrMapping {
public:
    // Create a root region.  This will span the entire aspace
    static status_t CreateRoot(VmAspace& aspace, uint32_t vmar_flags,
                               mxtl::RefPtr<VmAddressRegion>* out);
    // Create a subregion of this region
    status_t CreateSubVmar(size_t offset, size_t size, uint8_t align_pow2,
                           uint32_t vmar_flags, const char* name,
                           mxtl::RefPtr<VmAddressRegion>* out);
    // Create a VmMapping within this region
    status_t CreateVmMapping(size_t mapping_offset, size_t size, uint8_t align_pow2,
                             uint32_t vmar_flags,
                             mxtl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
                             uint arch_mmu_flags, const char* name,
                             mxtl::RefPtr<VmMapping>* out);

    // Find the child region that contains the given addr.  If addr is in a gap,
    // returns nullptr.  This is a non-recursive search.
    mxtl::RefPtr<VmAddressRegionOrMapping> FindRegion(vaddr_t addr);

    // Unmap a subset of the region of memory in the containing address space,
    // returning it to this region to allocate.  If a subregion is entirely in
    // the range, that subregion is destroyed.  If a subregion is partially in
    // the range, Unmap() will fail.
    status_t Unmap(vaddr_t base, size_t size);

    bool is_mapping() const override { return false; }

    void Dump(uint depth) const override;
    status_t PageFault(vaddr_t va, uint pf_flags) override;

protected:
    static const uint32_t kMagic = 0x564d4152; // VMAR

    friend class VmAspace;
    // constructor for use in creating the kernel aspace singleton
    explicit VmAddressRegion(VmAspace& kernel_aspace);
    // Count the allocated pages, caller must be holding the aspace lock
    size_t AllocatedPagesLocked() const override;

    friend class VmMapping;
    // Remove *region* from the subregion list
    void RemoveSubregion(VmAddressRegionOrMapping* region);

    friend mxtl::default_delete<VmAddressRegion>;
    ~VmAddressRegion() override;

private:
    // utility so WAVL tree can find the intrusive node for the child list
    using ChildList = mxtl::WAVLTree<vaddr_t, mxtl::RefPtr<VmAddressRegionOrMapping>,
                                     mxtl::DefaultKeyedObjectTraits<vaddr_t, VmAddressRegionOrMapping>,
                                     WAVLTreeTraits>;

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmAddressRegion);

    // private constructors, use Create...() instead
    VmAddressRegion(VmAspace& aspace, vaddr_t base, size_t size, uint32_t vmar_flags);
    VmAddressRegion(VmAddressRegion& parent, vaddr_t base, size_t size, uint32_t vmar_flags, const char* name);

    // Version of FindRegion() that does not acquire the aspace lock
    mxtl::RefPtr<VmAddressRegionOrMapping> FindRegionLocked(vaddr_t addr);

    // Version of Destroy() that does not acquire the aspace lock
    status_t DestroyLocked() override;

    void Activate() override;

    // Helper to share code between CreateSubVmar and CreateVmMapping
    status_t CreateSubVmarInternal(size_t offset, size_t size, uint8_t align_pow2,
                                   uint32_t vmar_flags,
                                   mxtl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
                                   uint arch_mmu_flags, const char* name,
                                   mxtl::RefPtr<VmAddressRegionOrMapping>* out);

    // internal utilities for interacting with the children list

    // returns true if it would be valid to create a child in the
    // range [base, base+size)
    bool IsRangeAvailableLocked(vaddr_t base, size_t size);

    // returns true if we can meet the allocation between the given children,
    // and if so populates pva with the base address to use.
    // TODO(teisenbe): Get rid of this once we implement randomization
    bool CheckGapLocked(const ChildList::iterator& prev, const ChildList::iterator& next,
                        vaddr_t* pva, vaddr_t search_base, vaddr_t align,
                        size_t region_size, size_t min_gap, uint arch_mmu_flags);

    // search for a spot to allocate for a region of a given size
    vaddr_t AllocSpotLocked(vaddr_t base, size_t size, uint8_t align_pow2,
                            size_t min_alloc_gap, uint arch_mmu_flags);

    // list of subregions, indexed by base address
    ChildList subregions_;
};

// A representation of the mapping of a VMO into the address space
class VmMapping final : public VmAddressRegionOrMapping,
                        public mxtl::DoublyLinkedListable<VmMapping *> {
public:
    // Accessors for VMO-mapping state
    uint arch_mmu_flags() const { return arch_mmu_flags_; }
    uint64_t object_offset() const { return object_offset_; }
    mxtl::RefPtr<VmObject> vmo() { return object_; };

    // Map in pages from the underlying vm object, optionally committing pages as it goes
    status_t MapRange(size_t offset, size_t len, bool commit);

    // Unmap a subset of the region of memory in the containing address space,
    // returning it to the parent region to allocate.  If all of the memory is unmapped,
    // Destroy()s this mapping.  If a subrange of the mapping is specified, the
    // mapping may be split.
    status_t Unmap(vaddr_t base, size_t size);

    // Change access permissions for this mapping.  It is an error to specify a
    // caching mode in the flags.  This will persist the caching mode the
    // mapping was created with.
    status_t Protect(uint arch_mmu_flags);

    bool is_mapping() const override { return true; }

    void Dump(uint depth) const override;
    status_t PageFault(vaddr_t va, uint pf_flags) override;

protected:
    static const uint32_t kMagic = 0x564d4150; // VMAP

    friend mxtl::default_delete<VmMapping>;
    ~VmMapping() override;

    // private apis from VmObject land
    friend class VmObjectPaged;

    // unmap any pages that map the passed in vmo range. May not intersect with this range
    status_t UnmapVmoRangeLocked(uint64_t start, uint64_t size);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VmMapping);

    // allow VmAddressRegion to manipulate VmMapping internals for construction
    // and bookkeeping
    friend class VmAddressRegion;

    // private constructors, use VmAddressRegion::Create...() instead
    VmMapping(VmAddressRegion& parent, vaddr_t base, size_t size, uint32_t vmar_flags,
              mxtl::RefPtr<VmObject> vmo, uint64_t vmo_offset, uint arch_mmu_flags,
              const char* name);

    // Version of Destroy() that does not acquire the aspace lock
    status_t DestroyLocked() override;

    // Implementation for Unmap().  This does not acquire the aspace lock, and
    // supports partial unmapping.
    status_t UnmapLocked(vaddr_t base, size_t size);

    // Version of AllocatedPages() that does not acquire the aspace lock
    size_t AllocatedPagesLocked() const override;

    void Activate() override;

    // Version of Activate that does not take the object_ lock
    void ActivateLocked();

    // pointer and region of the object we are mapping
    mxtl::RefPtr<VmObject> object_;
    uint64_t object_offset_ = 0;

    // cached mapping flags (read/write/user/etc)
    uint arch_mmu_flags_;
};
