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
                                                   bool compress_results,
                                                   dev_vaddr_t* mapped_addrs,
                                                   size_t mapped_addrs_count) {

    DEBUG_ASSERT(mapped_addrs);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(size));

    if (size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AutoLock guard(&lock_);

    if (zero_handles_) {
        return ZX_ERR_BAD_STATE;
    }

    fbl::unique_ptr<PinnedMemoryObject> pmo;
    zx_status_t status = PinnedMemoryObject::Create(*this, fbl::move(vmo),
                                                    offset, size, perms, &pmo);
    if (status != ZX_OK) {
        return status;
    }

    const fbl::Array<dev_vaddr_t>& pmo_addrs = pmo->mapped_addrs();
    const size_t found_addrs = pmo_addrs.size();
    if (compress_results) {
        if (found_addrs != mapped_addrs_count) {
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(mapped_addrs, pmo_addrs.get(), found_addrs * sizeof(dev_vaddr_t));
    } else {
        const size_t num_pages = size / PAGE_SIZE;
        if (num_pages != mapped_addrs_count) {
            return ZX_ERR_INVALID_ARGS;
        }
        const size_t min_contig = minimum_contiguity();
        size_t next_idx = 0;
        for (size_t i = 0; i < found_addrs; ++i) {
            dev_vaddr_t extent_base = pmo_addrs[i];
            for (dev_vaddr_t addr = extent_base;
                 addr < extent_base + min_contig && next_idx < num_pages;
                 addr += PAGE_SIZE, ++next_idx) {
                mapped_addrs[next_idx] = addr;
            }
        }
    }

    pinned_memory_.push_back(fbl::move(pmo));
    return ZX_OK;
}

zx_status_t BusTransactionInitiatorDispatcher::Unpin(const dev_vaddr_t base_addr) {
    fbl::AutoLock guard(&lock_);

    if (zero_handles_) {
        return ZX_ERR_BAD_STATE;
    }

    for (auto& pmo : pinned_memory_) {
        const fbl::Array<dev_vaddr_t>& pmo_addrs = pmo.mapped_addrs();
        if (pmo_addrs[0] == base_addr) {
            // The PMO dtor will take care of the actual unpinning.
            pinned_memory_.erase(pmo);
            return ZX_OK;
        }
    }

    return ZX_ERR_INVALID_ARGS;
}

void BusTransactionInitiatorDispatcher::on_zero_handles() {
    fbl::AutoLock guard(&lock_);
    while (!pinned_memory_.is_empty()) {
        pinned_memory_.pop_front();
    }
    zero_handles_ = true;
}
