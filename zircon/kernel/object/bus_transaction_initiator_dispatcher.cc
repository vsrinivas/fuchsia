// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/bus_transaction_initiator_dispatcher.h"

#include <align.h>
#include <err.h>
#include <lib/counters.h>
#include <lib/debuglog.h>
#include <zircon/rights.h>

#include <new>

#include <dev/iommu.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>
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

  Guard<Mutex> guard{get_lock()};
  // TODO(fxbug.dev/56205): When the time has come to switch this over from a warning
  // to enforcement, come back here and delete the #else half of this #ifdef.
#if 0
  // User may not pin new memory if either our BTI has hit zero handles, or if
  // we currently have quarantined pages.  In the case that there are active
  // quarantined pages, driver code is expected to take the steps to stop their
  // DMA, and then release the quarantine before proceeding to pin new memory.
  if (zero_handles_ || !quarantine_.is_empty()) {
    return ZX_ERR_BAD_STATE;
  }
#else
  if (zero_handles_) {
    return ZX_ERR_BAD_STATE;
  }

  if (!quarantine_.is_empty()) {
    char proc_name[ZX_MAX_NAME_LEN] = {0};
    char thread_name[ZX_MAX_NAME_LEN] = {0};
    char bti_name[ZX_MAX_NAME_LEN] = {0};

    // If we have no current thread dispatcher, then this is a kernel thread.  We
    // have no process to report, just report that the action was taken by a
    // kernel thread and leave it at that.
    ThreadDispatcher* thread_disp = ThreadDispatcher::GetCurrent();
    if (thread_disp == nullptr) {
      snprintf(proc_name, sizeof(proc_name), "<kernel>");
      snprintf(thread_name, sizeof(thread_name), "<kernel>");
    } else {
      // Get the name of the user mode process and thread which closed the handle
      // to the object which eventually resulted in the leak.
      ProcessDispatcher::GetCurrent()->get_name(proc_name);
      thread_disp->get_name(thread_name);
    }

    // Fetch the BTI name (if any).
    this->get_name(bti_name);

    // If any of these strings are empty, replace them with just "<unknown>".
    if (!proc_name[0]) {
      snprintf(proc_name, sizeof(proc_name), "<unknown>");
    }
    if (!thread_name[0]) {
      snprintf(thread_name, sizeof(thread_name), "<unknown>");
    }
    if (!bti_name[0]) {
      snprintf(bti_name, sizeof(bti_name), "<unknown>");
    }

    printf(
        "KERN: Bus Transaction Initiator (ID 0x%lx, name \"%s\") was asked to pin a VMO while "
        "there were still pages in the quarantine list. Requesting process/thread was \"%s\", "
        "thread \"%s\". User mode code needs to be updated to follow the quarantine protocol.\n",
        bti_id_, bti_name, proc_name, thread_name);
  }
#endif

  return PinnedMemoryTokenDispatcher::Create(fbl::RefPtr(this), ktl::move(pinned_vmo), perms,
                                             pmt_handle, pmt_rights);
}

void BusTransactionInitiatorDispatcher::ReleaseQuarantine() {
  QuarantineList tmp;

  // The PMT dtor will call RemovePmo, which will reacquire this BTI's lock.
  // To avoid deadlock, drop the lock before letting the quarantined PMTs go.
  {
    Guard<Mutex> guard{get_lock()};
    quarantine_.swap(tmp);
  }
}

void BusTransactionInitiatorDispatcher::on_zero_handles() {
  Guard<Mutex> guard{get_lock()};
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
    PrintQuarantineWarningLocked(BtiPageLeakReason::BtiClose);
  }
}

zx_status_t BusTransactionInitiatorDispatcher::set_name(const char* name, size_t len) {
  // The kernel implementation of fbl::Name is protected using an internal
  // spinlock.  No need for any special locks here.
  return name_.set(name, len);
}

void BusTransactionInitiatorDispatcher::get_name(char out_name[ZX_MAX_NAME_LEN]) const {
  // The kernel implementation of fbl::Name is protected using an internal
  // spinlock.  No need for any special locks here.
  name_.get(ZX_MAX_NAME_LEN, out_name);
}

void BusTransactionInitiatorDispatcher::AddPmoLocked(PinnedMemoryTokenDispatcher* pmt) {
  DEBUG_ASSERT(!fbl::InContainer<PmtListTag>(*pmt));
  pinned_memory_.push_back(pmt);
}

void BusTransactionInitiatorDispatcher::RemovePmo(PinnedMemoryTokenDispatcher* pmt) {
  Guard<Mutex> guard{get_lock()};
  DEBUG_ASSERT(fbl::InContainer<PmtListTag>(*pmt));
  pinned_memory_.erase(*pmt);
}

void BusTransactionInitiatorDispatcher::Quarantine(fbl::RefPtr<PinnedMemoryTokenDispatcher> pmt) {
  Guard<Mutex> guard{get_lock()};

  DEBUG_ASSERT(fbl::InContainer<PmtListTag>(*pmt));
  quarantine_.push_back(ktl::move(pmt));

  if (zero_handles_) {
    // If we quarantine when at zero handles, this PMT will be leaked.  See
    // the comment in on_zero_handles().
    PrintQuarantineWarningLocked(BtiPageLeakReason::PmtClose);
  }
}

// The count of the pinned memory object tokens.
uint64_t BusTransactionInitiatorDispatcher::pmo_count() const {
  Guard<Mutex> guard{get_lock()};
  return pinned_memory_.size_slow();
}

// The count of the quarantined pinned memory object tokens.
uint64_t BusTransactionInitiatorDispatcher::quarantine_count() const {
  Guard<Mutex> guard{get_lock()};
  return quarantine_.size_slow();
}

void BusTransactionInitiatorDispatcher::PrintQuarantineWarningLocked(BtiPageLeakReason reason) {
  uint64_t leaked_pages = 0;
  size_t num_entries = 0;
  for (const auto& pmt : quarantine_) {
    leaked_pages += pmt.size() / PAGE_SIZE;
    num_entries++;
  }

  char proc_name[ZX_MAX_NAME_LEN] = {0};
  char thread_name[ZX_MAX_NAME_LEN] = {0};
  char bti_name[ZX_MAX_NAME_LEN] = {0};

  // If we have no current thread dispatcher, then this is a kernel thread.  We
  // have no process to report, just report that the action was taken by a
  // kernel thread and leave it at that.
  ThreadDispatcher* thread_disp = ThreadDispatcher::GetCurrent();
  if (thread_disp == nullptr) {
    snprintf(proc_name, sizeof(proc_name), "<kernel>");
    snprintf(thread_name, sizeof(thread_name), "<kernel>");
  } else {
    // Get the name of the user mode process and thread which closed the handle
    // to the object which eventually resulted in the leak.
    ProcessDispatcher::GetCurrent()->get_name(proc_name);
    thread_disp->get_name(thread_name);
  }

  // Fetch the BTI name (if any).
  this->get_name(bti_name);

  // If any of these strings are empty, replace them with just "<unknown>".
  if (!proc_name[0]) {
    snprintf(proc_name, sizeof(proc_name), "<unknown>");
  }
  if (!thread_name[0]) {
    snprintf(thread_name, sizeof(thread_name), "<unknown>");
  }
  if (!bti_name[0]) {
    snprintf(bti_name, sizeof(bti_name), "<unknown>");
  }

  // Finally, print the message describing the leak, as best we can.
  const char* leak_cause;
  switch (reason) {
    case BtiPageLeakReason::BtiClose:
      leak_cause = "a BTI being closed with a non-empty quarantine list";
      break;

    case BtiPageLeakReason::PmtClose:
      leak_cause = "a pinned PMT being closed, when the BTI used to pin it was already closed";
      break;

    default:
      leak_cause = "<unknown>";
      break;
  }

  // TODO(fxbug.dev/56157): Make this an OOPS once the driver bugs are fixed.
  printf("KERN: Bus Transaction Initiator (ID 0x%lx, name \"%s\") has leaked %" PRIu64
         " pages in %zu VMOs. Leak was caused by %s. The last handle was closed by process "
         "\"%s\", and thread \"%s\"\n",
         bti_id_, bti_name, leaked_pages, num_entries, leak_cause, proc_name, thread_name);
}
