// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/bus_transaction_initiator_dispatcher.h>

#include <dev/iommu.h>
#include <err.h>
#include <vm/vm_object.h>
#include <zircon/rights.h>
#include <zxcpp/new.h>
#include <fbl/auto_lock.h>

zx_status_t BusTransactionInitiatorDispatcher::Create(fbl::RefPtr<Iommu> iommu, uint64_t bti_id,
                                                      fbl::RefPtr<Dispatcher>* dispatcher,
                                                      zx_rights_t* rights) {

    if (!iommu->IsValidBusTxnId(bti_id)) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    auto disp = new (&ac) BusTransactionInitiatorDispatcher(fbl::move(iommu), bti_id);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    *rights = ZX_DEFAULT_BTI_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

BusTransactionInitiatorDispatcher::BusTransactionInitiatorDispatcher(fbl::RefPtr<Iommu> iommu,
                                                                     uint64_t bti_id)
        : iommu_(fbl::move(iommu)), bti_id_(bti_id), zero_handles_(false) {}

BusTransactionInitiatorDispatcher::~BusTransactionInitiatorDispatcher() {
    DEBUG_ASSERT(pinned_memory_.is_empty());
}

zx_status_t BusTransactionInitiatorDispatcher::Pin(fbl::RefPtr<VmObject> vmo, uint64_t offset,
                                                   uint64_t size, uint32_t perms,
                                                   fbl::RefPtr<Dispatcher>* pmt,
                                                   zx_rights_t* pmt_rights) {

    DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(size));

    if (size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AutoLock guard(&lock_);

    if (zero_handles_) {
        return ZX_ERR_BAD_STATE;
    }

    return PinnedMemoryTokenDispatcher::Create(fbl::WrapRefPtr(this), fbl::move(vmo),
                                               offset, size, perms, pmt, pmt_rights);
}

zx_status_t BusTransactionInitiatorDispatcher::Unpin(const dev_vaddr_t base_addr) {
    fbl::AutoLock guard(&lock_);

    if (zero_handles_) {
        return ZX_ERR_BAD_STATE;
    }

    for (auto& pmt : legacy_pinned_memory_) {
        const fbl::Array<dev_vaddr_t>& pmt_addrs = pmt.mapped_addrs();
        if (pmt_addrs[0] == base_addr) {
            // The PMT dtor will take care of the actual unpinning.
            auto ptr = legacy_pinned_memory_.erase(pmt);
            // When |ptr| goes out of scope, its dtor will be run.  Drop the
            // lock since the dtor will call RemovePmo, which takes the lock.
            guard.release();
            return ZX_OK;
        }
    }

    return ZX_ERR_INVALID_ARGS;
}

void BusTransactionInitiatorDispatcher::on_zero_handles() {
    // We need to drop the lock before letting PMT dtors run, since the
    // PMT dtor calls RemovePmo, which takes the lock again.
    LegacyPmoList tmp;
    {
        fbl::AutoLock guard(&lock_);
        // Prevent new pinning from happening.  The Dispatcher will stick around
        // until all of the PMTs are closed.
        zero_handles_ = true;

        legacy_pinned_memory_.swap(tmp);
    }

}

void BusTransactionInitiatorDispatcher::AddPmoLocked(PinnedMemoryTokenDispatcher* pmt) {
    DEBUG_ASSERT(!pmt->dll_pmt_.InContainer());
    pinned_memory_.push_back(pmt);
}

void BusTransactionInitiatorDispatcher::RemovePmo(PinnedMemoryTokenDispatcher* pmt) {
    fbl::AutoLock guard(&lock_);
    DEBUG_ASSERT(pmt->dll_pmt_.InContainer());
    pinned_memory_.erase(*pmt);
}

void BusTransactionInitiatorDispatcher::ConvertToLegacy(
        fbl::RefPtr<PinnedMemoryTokenDispatcher> pmt) {
    fbl::AutoLock guard(&lock_);

    legacy_pinned_memory_.push_back(fbl::move(pmt));
}
