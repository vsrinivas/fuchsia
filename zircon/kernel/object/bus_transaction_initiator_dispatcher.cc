// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/bus_transaction_initiator_dispatcher.h"

#include <err.h>
#include <lib/counters.h>
#include <zircon/rights.h>

#include <new>

#include <dev/iommu.h>
#include <vm/pinned_vm_object.h>
#include <vm/vm_object.h>

KCOUNTER(dispatcher_bti_create_count, "dispatcher.bti.create")
KCOUNTER(dispatcher_bti_destroy_count, "dispatcher.bti.destroy")

zx_status_t BusTransactionInitiatorDispatcher::Create(
    fbl::RefPtr<Iommu> iommu, uint64_t bti_id,
    KernelHandle<BusTransactionInitiatorDispatcher>* handle, zx_rights_t* rights) {
  if (!iommu->IsValidBusTxnId(bti_id)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  KernelHandle new_handle(
      fbl::AdoptRef(new (&ac) BusTransactionInitiatorDispatcher(ktl::move(iommu), bti_id)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  *rights = default_rights();
  *handle = ktl::move(new_handle);
  return ZX_OK;
}

BusTransactionInitiatorDispatcher::BusTransactionInitiatorDispatcher(fbl::RefPtr<Iommu> iommu,
                                                                     uint64_t bti_id)
    : iommu_(ktl::move(iommu)), bti_id_(bti_id), zero_handles_(false) {
  kcounter_add(dispatcher_bti_create_count, 1);
}

BusTransactionInitiatorDispatcher::~BusTransactionInitiatorDispatcher() {
  DEBUG_ASSERT(pinned_memory_.is_empty());
  kcounter_add(dispatcher_bti_destroy_count, 1);
}

zx_status_t BusTransactionInitiatorDispatcher::Pin(
    fbl::RefPtr<VmObject> vmo, uint64_t offset, uint64_t size, uint32_t perms,
    KernelHandle<PinnedMemoryTokenDispatcher>* pmt_handle, zx_rights_t* pmt_rights) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size));

  if (size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  PinnedVmObject pinned_vmo;
  zx_status_t status = PinnedVmObject::Create(vmo, offset, size, &pinned_vmo);
  if (status != ZX_OK) {
    return status;
  }

  Guard<fbl::Mutex> guard{get_lock()};
  if (zero_handles_) {
    return ZX_ERR_BAD_STATE;
  }

  return PinnedMemoryTokenDispatcher::Create(fbl::RefPtr(this), ktl::move(pinned_vmo), perms,
                                             pmt_handle, pmt_rights);
}

void BusTransactionInitiatorDispatcher::ReleaseQuarantine() {
  QuarantineList tmp;

  // The PMT dtor will call RemovePmo, which will reacquire this BTI's lock.
  // To avoid deadlock, drop the lock before letting the quarantined PMTs go.
  {
    Guard<fbl::Mutex> guard{get_lock()};
    quarantine_.swap(tmp);
  }
}

void BusTransactionInitiatorDispatcher::on_zero_handles() {
  Guard<fbl::Mutex> guard{get_lock()};
  // Prevent new pinning from happening.  The Dispatcher will stick around
  // until all of the PMTs are closed.
  zero_handles_ = true;

  // Do not clear out the quarantine list.  PMTs hold a reference to the BTI
  // and the BTI holds a reference to each quarantined PMT.  We intentionally
  // leak the BTI, all quarantined PMTs, and their underlying VMOs.  We could
  // get away with freeing the BTI and the PMTs, but for safety we must leak
  // at least the pinned parts of the VMOs, since we have no assurance that
  // hardware is not still reading/writing to it.
  if (!quarantine_.is_empty()) {
    PrintQuarantineWarningLocked();
  }
}

void BusTransactionInitiatorDispatcher::AddPmoLocked(PinnedMemoryTokenDispatcher* pmt) {
  DEBUG_ASSERT(!pmt->dll_pmt_.InContainer());
  pinned_memory_.push_back(pmt);
}

void BusTransactionInitiatorDispatcher::RemovePmo(PinnedMemoryTokenDispatcher* pmt) {
  Guard<fbl::Mutex> guard{get_lock()};
  DEBUG_ASSERT(pmt->dll_pmt_.InContainer());
  pinned_memory_.erase(*pmt);
}

void BusTransactionInitiatorDispatcher::Quarantine(fbl::RefPtr<PinnedMemoryTokenDispatcher> pmt) {
  Guard<fbl::Mutex> guard{get_lock()};

  DEBUG_ASSERT(pmt->dll_pmt_.InContainer());
  quarantine_.push_back(ktl::move(pmt));

  if (zero_handles_) {
    // If we quarantine when at zero handles, this PMT will be leaked.  See
    // the comment in on_zero_handles().
    PrintQuarantineWarningLocked();
  }
}

// The count of the pinned memory object tokens.
uint64_t BusTransactionInitiatorDispatcher::pmo_count() const {
  Guard<fbl::Mutex> guard{get_lock()};
  return pinned_memory_.size_slow();
}

// The count of the quarantined pinned memory object tokens.
uint64_t BusTransactionInitiatorDispatcher::quarantine_count() const {
  Guard<fbl::Mutex> guard{get_lock()};
  return quarantine_.size_slow();
}

void BusTransactionInitiatorDispatcher::PrintQuarantineWarningLocked() {
  uint64_t leaked_pages = 0;
  size_t num_entries = 0;
  for (const auto& pmt : quarantine_) {
    leaked_pages += pmt.size() / PAGE_SIZE;
    num_entries++;
  }
  printf("Bus Transaction Initiator 0x%lx has leaked %" PRIu64 " pages in %zu VMOs\n", bti_id_,
         leaked_pages, num_entries);
}
