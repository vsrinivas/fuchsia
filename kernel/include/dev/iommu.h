// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <vm/vm_object.h>
#include <zircon/thread_annotations.h>

#include <stdbool.h>
#include <sys/types.h>
#include <inttypes.h>

#define IOMMU_FLAG_PERM_READ    (1<<0)
#define IOMMU_FLAG_PERM_WRITE   (1<<1)
#define IOMMU_FLAG_PERM_EXECUTE (1<<2)

// Type used to refer to virtual addresses presented to a device by the IOMMU.
typedef uint64_t dev_vaddr_t;

class Iommu : public fbl::RefCounted<Iommu>,
              public fbl::DoublyLinkedListable<fbl::RefPtr<Iommu>> {
public:
    // Check if |bus_txn_id| is valid for this IOMMU (i.e. could be used
    // to configure a device).
    virtual bool IsValidBusTxnId(uint64_t bus_txn_id) const = 0;

    // Grant the device identified by |bus_txn_id| access to the range of
    // pages given by [offset, offset + size) in |vmo|.  The base of the
    // mapped range is returned via |vaddr|.  The number of bytes mapped is
    // returned via |mapped_len|. |vaddr| and |mapped_len| must not be NULL.
    //
    // |mapped_len| may be more than |size|, in the event that |size| is
    // not page-aligned.  |mapped_len| will always be page-aligned.
    //
    // The memory in the given range of |vmo| MUST have been pined before
    // calling this function, and if this function returns ZX_OK,
    // MUST NOT be unpined until after Unmap() is called on the returned range.
    //
    // |perms| defines the access permissions, using the IOMMU_FLAG_PERM_*
    // flags.
    //
    // If |size| is no more than |minimum_contiguity()|, this will never return
    // a partial mapping.
    //
    // Returns ZX_ERR_INVALID_ARGS if:
    //  |size| is zero.
    //  |offset| is not aligned to PAGE_SIZE
    // Returns ZX_ERR_OUT_OF_RANGE if [offset, offset + size) is not a valid range in |vmo|.
    // Returns ZX_ERR_NOT_FOUND if |bus_txn_id| is not valid.
    // Returns ZX_ERR_NO_RESOURCES if the mapping could not be made due to lack
    // of an available address range.
    virtual zx_status_t Map(uint64_t bus_txn_id, const fbl::RefPtr<VmObject>& vmo,
                            uint64_t offset, size_t size, uint32_t perms,
                            dev_vaddr_t* vaddr, size_t* mapped_len) = 0;

    // Revoke access to the range of addresses [vaddr, vaddr + size) for the
    // device identified by |bus_txn_id|.
    //
    // Returns ZX_ERR_INVALID_ARGS if:
    //  |size| is not a multiple of PAGE_SIZE
    //  |vaddr| is not aligned to PAGE_SIZE
    // Returns ZX_ERR_NOT_FOUND if |bus_txn_id| is not valid.
    virtual zx_status_t Unmap(uint64_t bus_txn_id, dev_vaddr_t vaddr, size_t size) = 0;

    // Remove all mappings for |bus_txn_id|.
    // Returns ZX_ERR_NOT_FOUND if |bus_txn_id| is not valid.
    virtual zx_status_t ClearMappingsForBusTxnId(uint64_t bus_txn_id) = 0;

    // Returns the number of bytes that Map() can guarantee, upon success, to find
    // a contiguous address range for.  This function is only returns meaningful
    // values if |IsValidBusTxnId(bus_txn_id)|.
    virtual uint64_t minimum_contiguity(uint64_t bus_txn_id) const = 0;

    // Returns the total size of the space the addresses are mapped into.  This
    // function is only returns meaningful values if |IsValidBusTxnId(bus_txn_id)|.
    virtual uint64_t aspace_size(uint64_t bus_txn_id) const = 0;

    virtual ~Iommu() { }
};
