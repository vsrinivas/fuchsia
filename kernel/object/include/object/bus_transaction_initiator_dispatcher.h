// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/iommu.h>
#include <fbl/canary.h>
#include <fbl/mutex.h>
#include <object/dispatcher.h>
#include <object/pinned_memory_object.h>

#include <sys/types.h>

class Iommu;

class BusTransactionInitiatorDispatcher final : public SoloDispatcher {
public:
    static zx_status_t Create(fbl::RefPtr<Iommu> iommu, uint64_t bti_id,
                              fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights);

    ~BusTransactionInitiatorDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_BTI; }
    bool has_state_tracker() const final { return true; }

    // Pins the given VMO range and writes the addresses into |mapped_addrs|.
    //
    // |mapped_addrs_count| must be either
    // 1) If |compress_results|, |size|/|minimum_contiguity()|, rounded up, in which
    // case each returned address represents a run of |minimum_contiguity()| bytes (with
    // the exception of the last which may be short)
    // 2) Otherwise, |size|/|PAGE_SIZE|, in which case each returned address represents a
    // single page.
    //
    // Returns ZX_ERR_INVALID_ARGS if |offset| or |size| are not PAGE_SIZE aligned.
    // Returns ZX_ERR_INVALID_ARGS if |perms| is not suitable to pass to the Iommu::Map() interface.
    // Returns ZX_ERR_INVALID_ARGS if |mapped_addrs_count| is not exactly the
    //   value described above.
    zx_status_t Pin(fbl::RefPtr<VmObject> vmo, uint64_t offset, uint64_t size, uint32_t perms,
                    bool compress_results, dev_vaddr_t* mapped_addrs, size_t mapped_addrs_count);

    // Unpins the region previously created by Pin() that starts with |base_addr|.
    // Returns an error if |base_addr| does not correspond to something returned
    // by a previous call to Pin().
    zx_status_t Unpin(dev_vaddr_t base_addr);

    void on_zero_handles() final;

    fbl::RefPtr<Iommu> iommu() const { return iommu_; }
    uint64_t bti_id() const { return bti_id_; }

    // Pin will always be able to return addresses that are contiguous for at
    // least this many bytes.  E.g. if this returns 1MB, then a call to Pin()
    // with a size of 2MB will return at most two physically-contiguous runs.  If the size
    // were 2.5MB, it will return at most three physically-contiguous runs.
    uint64_t minimum_contiguity() const { return iommu_->minimum_contiguity(bti_id_); }

    // The number of bytes in the address space (UINT64_MAX if 2^64).
    uint64_t aspace_size() const { return iommu_->aspace_size(bti_id_); }

private:
    BusTransactionInitiatorDispatcher(fbl::RefPtr<Iommu> iommu, uint64_t bti_id);

    fbl::Canary<fbl::magic("BTID")> canary_;

    fbl::Mutex lock_;
    const fbl::RefPtr<Iommu> iommu_;
    const uint64_t bti_id_;

    fbl::DoublyLinkedList<fbl::unique_ptr<PinnedMemoryObject>> pinned_memory_ TA_GUARDED(lock_);
    bool zero_handles_ TA_GUARDED(lock_);
};
