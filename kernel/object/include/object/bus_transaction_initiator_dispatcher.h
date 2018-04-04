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
#include <object/pinned_memory_token_dispatcher.h>

#include <sys/types.h>

class Iommu;

class BusTransactionInitiatorDispatcher final : public SoloDispatcher {
public:
    static zx_status_t Create(fbl::RefPtr<Iommu> iommu, uint64_t bti_id,
                              fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights);

    ~BusTransactionInitiatorDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_BTI; }
    bool has_state_tracker() const final { return true; }

    // Pins the given VMO range and returns an PinnedMemoryTokenDispatcher
    // representing the pinned range.
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
                    fbl::RefPtr<Dispatcher>* pmt, zx_rights_t* rights);

    // Releases all quarantined PMTs.  The memory pins are released and the VMO
    // references are dropped, so the underlying VMOs may be immediately destroyed, and the
    // underlying physical memory may be reallocated.
    void ReleaseQuarantine();

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

protected:
    friend PinnedMemoryTokenDispatcher;

    // Used to register a PMT pointer during PMT construction
    void AddPmoLocked(PinnedMemoryTokenDispatcher* pmt) TA_REQ(lock_);
    // Used to unregister a PMT pointer during PMT destruction
    void RemovePmo(PinnedMemoryTokenDispatcher* pmt);

    // Remove |pmt| from pinned_memory_ and append it to the quarantine_ list.
    // This will prevent its underlying VMO from being unpinned until the
    // quarantine is cleared.
    void Quarantine(fbl::RefPtr<PinnedMemoryTokenDispatcher> pmt) TA_EXCL(lock_);

private:
    BusTransactionInitiatorDispatcher(fbl::RefPtr<Iommu> iommu, uint64_t bti_id);
    void PrintQuarantineWarningLocked() TA_REQ(lock_);

    fbl::Canary<fbl::magic("BTID")> canary_;

    // TODO(teisenbe): Unify this lock with the SoloDispatcher lock
    fbl::Mutex lock_;
    const fbl::RefPtr<Iommu> iommu_;
    const uint64_t bti_id_;

    using PmoList = fbl::DoublyLinkedList<PinnedMemoryTokenDispatcher*,
          PinnedMemoryTokenDispatcher::PinnedMemoryTokenListTraits>;
    PmoList pinned_memory_ TA_GUARDED(lock_);

    using QuarantineList = fbl::DoublyLinkedList<fbl::RefPtr<PinnedMemoryTokenDispatcher>,
          PinnedMemoryTokenDispatcher::QuarantineListTraits>;
    QuarantineList quarantine_ TA_GUARDED(lock_);

    bool zero_handles_ TA_GUARDED(lock_);
};
