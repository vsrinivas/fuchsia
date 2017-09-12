// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <zircon/thread_annotations.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

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
    // physical addresses given by [paddr, paddr + size).  The base of the
    // mapped range is returned via |vaddr|.  |vaddr| must not be NULL.
    //
    // |perms| defines the access permissions, using the IOMMU_FLAG_PERM_*
    // flags.
    //
    // Returns ZX_ERR_INVALID_ARGS if:
    //  |size| is not a multiple of PAGE_SIZE
    //  |paddr| is not aligned to PAGE_SIZE
    // Returns ZX_ERR_NOT_FOUND if |bus_txn_id| is not valid.
    virtual zx_status_t Map(uint64_t bus_txn_id, paddr_t paddr, size_t size, uint32_t perms,
                            dev_vaddr_t* vaddr) = 0;

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

    virtual ~Iommu() { }
};
