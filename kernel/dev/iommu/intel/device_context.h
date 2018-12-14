// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <ktl/unique_ptr.h>
#include <fbl/vector.h>
#include <region-alloc/region-alloc.h>
#include <vm/vm_object.h>

#include "hw.h"
#include "second_level_pt.h"

namespace intel_iommu {

class IommuImpl;

class DeviceContext : public fbl::DoublyLinkedListable<ktl::unique_ptr<DeviceContext>> {
public:
    ~DeviceContext();

    // Create a new DeviceContext representing the given BDF.  It is a fatal error
    // to try to create a context for a BDF that already has one.
    static zx_status_t Create(ds::Bdf bdf, uint32_t domain_id, IommuImpl* parent,
                              volatile ds::ExtendedContextEntry* context_entry,
                              ktl::unique_ptr<DeviceContext>* device);
    static zx_status_t Create(ds::Bdf bdf, uint32_t domain_id, IommuImpl* parent,
                              volatile ds::ContextEntry* context_entry,
                              ktl::unique_ptr<DeviceContext>* device);

    // Check if this DeviceContext is for the given BDF
    bool is_bdf(ds::Bdf bdf) const {
        return bdf_ == bdf;
    }

    uint32_t domain_id() const { return domain_id_; }

    uint64_t minimum_contiguity() const;
    uint64_t aspace_size() const;

    // Use the second-level translation table to map the host pages in the given
    // range on |vmo| to the guest's address |*virt_paddr|.  |size| is in bytes.
    // |mapped_len| may be larger than |size|, if |size| was not page-aligned.
    //
    // If |map_contiguous| is false, this function may return a partial mapping,
    // in which case |mapped_len| will indicate how many bytes were actually mapped.
    //
    // If |map_contiguous| is true, this function will never return a partial
    // mapping, and |mapped_len| should be equal to |size|.
    zx_status_t SecondLevelMap(const fbl::RefPtr<VmObject>& vmo,
                               uint64_t offset, size_t size, uint32_t perms,
                               bool map_contiguous, paddr_t* virt_paddr, size_t* mapped_len);
    zx_status_t SecondLevelUnmap(paddr_t virt_paddr, size_t size);

    // Use the second-level translation table to identity-map the given range of
    // host pages.
    zx_status_t SecondLevelMapIdentity(paddr_t base, size_t size, uint32_t perms);

private:
    DeviceContext(ds::Bdf bdf, uint32_t domain_id, IommuImpl* parent,
                  volatile ds::ExtendedContextEntry* context_entry);
    DeviceContext(ds::Bdf bdf, uint32_t domain_id, IommuImpl* parent,
                  volatile ds::ContextEntry* context_entry);

    DISALLOW_COPY_ASSIGN_AND_MOVE(DeviceContext);

    // Shared initialization code for the two public Create() methods
    zx_status_t InitCommon();

    // Map a VMO which may consist of discontiguous physical pages. If
    // |map_contiguous| is true, this must either map the whole requested range
    // contiguously, or fail. If |map_contiguous| is false, it may return
    // success with a partial mapping.
    zx_status_t SecondLevelMapDiscontiguous(const fbl::RefPtr<VmObject>& vmo,
                                            uint64_t offset, size_t size, uint flags,
                                            bool map_contiguous, paddr_t* virt_paddr,
                                            size_t* mapped_len);

    // Map a VMO which consists of contiguous physical pages. Currently we assume
    // that all contiguous VMOs should be mapped as a contiguous range, so this
    // function will not return a partial mapping.
    zx_status_t SecondLevelMapContiguous(const fbl::RefPtr<VmObject>& vmo,
                                         uint64_t offset, size_t size, uint flags,
                                         paddr_t* virt_paddr, size_t* mapped_len);

    IommuImpl* const parent_;
    union {
        volatile ds::ExtendedContextEntry* const extended_context_entry_;
        volatile ds::ContextEntry* const context_entry_;
    };

    // Page tables used for translating requests-without-PASID and for nested
    // translation of requests-with-PASID.
    SecondLevelPageTable second_level_pt_;
    RegionAllocator region_alloc_;
    // TODO(teisenbe): Use a better data structure for these.  If the region
    // nodes were intrusive, we wouldn't need to have a resizable array for this
    // and we could have cheaper removal.  We can fix this up when it's a
    // problem though.
    //
    // TODO(johngro): @teisenbe.  I think that the efficient solution here would
    // be to slightly tweak the way that Regions handed out from the
    // RegionAllocator are structured.  Currently, they are handed out as
    // unique_ptrs of const Region* so that users may not mess with *any* of the
    // bookkeeping structure of the region.
    //
    // It should be rather simple to alter this so that the start/len book is
    // read-only because of the class contract (inherit Region privately from
    // ralloc_region_t instead of publically, then add some accessors for the
    // start/len), and then hand out mutable pointers instead of const pointer.
    // All that should be needed after that is some storage for your instrusive
    // state.  Currently, there are already 2 WAVL nodes which are part of this
    // structure but not used when the structure is owned by a user.
    //
    // We could just add some state which is dedicated to the user's list node,
    // or we could repurpose the storage already available to us because of the
    // WAVL state and allow the user to make use of this storage however they
    // want to.  The latter option involves some grungy casting, but would mean
    // that you do not need to maintain a vector or perform *any* dynamic
    // allocation at this level, and still not have to pay any price in terms of
    // storage in order to do so.
    //
    // LMK if any of this is interesting to you.  I can put together a patch and
    // send it your way if you want.
    fbl::Vector<RegionAllocator::Region::UPtr> allocated_regions_;

    const ds::Bdf bdf_;
    const bool extended_;
    const uint32_t domain_id_;
};

} // namespace intel_iommu
