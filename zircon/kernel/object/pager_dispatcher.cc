// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>
#include <trace.h>
#include <zircon/syscalls-next.h>

#include <kernel/thread.h>
#include <object/pager_dispatcher.h>
#include <object/pager_proxy.h>
#include <object/thread_dispatcher.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_pager_create_count, "dispatcher.pager.create")
KCOUNTER(dispatcher_pager_destroy_count, "dispatcher.pager.destroy")

zx_status_t PagerDispatcher::Create(KernelHandle<PagerDispatcher>* handle, zx_rights_t* rights) {
  fbl::AllocChecker ac;
  KernelHandle new_handle(fbl::AdoptRef(new (&ac) PagerDispatcher()));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  *rights = default_rights();
  *handle = ktl::move(new_handle);
  return ZX_OK;
}

PagerDispatcher::PagerDispatcher() : SoloDispatcher() {
  kcounter_add(dispatcher_pager_create_count, 1);
}

PagerDispatcher::~PagerDispatcher() {
  DEBUG_ASSERT(proxies_.is_empty());
  kcounter_add(dispatcher_pager_destroy_count, 1);
}

zx_status_t PagerDispatcher::CreateSource(fbl::RefPtr<PortDispatcher> port, uint64_t key,
                                          uint32_t options, fbl::RefPtr<PageSource>* src_out) {
  Guard<Mutex> guard{&lock_};
  // Make sure on_zero_handles has not been called. This could happen if a call to pager_create_vmo
  // races with closing the last handle, as pager_create_vmo does not hold the handle table lock
  // over this operation.
  if (triggered_zero_handles_) {
    return ZX_ERR_BAD_STATE;
  }

  // Process any options relevant to creation of the PagerProxy.
  uint32_t proxy_options = 0;
  if (options & ZX_VMO_TRAP_DIRTY) {
    proxy_options = PagerProxy::kTrapDirty;
    options &= ~ZX_VMO_TRAP_DIRTY;
  }
  if (options) {
    return ZX_ERR_INVALID_ARGS;
  }

  // We are going to setup two objects that both need to point to each other. As such one of the
  // pointers must be bound 'late' and not in the constructor.
  fbl::AllocChecker ac;
  auto proxy =
      fbl::MakeRefCountedChecked<PagerProxy>(&ac, this, ktl::move(port), key, proxy_options);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto src = fbl::MakeRefCountedChecked<PageSource>(&ac, proxy);

  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  // Now that PageSource has been created and has a reference to proxy we must setup expected
  // backlink in proxy. As such there must never be an early return added between here and
  // SetPagerSourceUnchecked.

  // Setting this creates a RefPtr cycle between the PagerProxy and PageSource, however we guarantee
  // we will call proxy->OnDispatcherClose at some point to break the cycle.
  proxy->SetPageSourceUnchecked(src);

  proxies_.push_front(ktl::move(proxy));
  *src_out = ktl::move(src);
  return ZX_OK;
}

fbl::RefPtr<PagerProxy> PagerDispatcher::ReleaseProxy(PagerProxy* proxy) {
  Guard<Mutex> guard{&lock_};
  // proxy might not be in the container since we could be racing with a call to on_zero_handles,
  // but that should only happen if we have triggered_zero_handles_. Note that it is possible for
  // the proxy to still be in the container even if triggered_zero_handles_ is true, as we drop the
  // lock between OnDispatcherClose calls for each proxy in the list, so we might not have gotten to
  // this proxy yet.
  DEBUG_ASSERT_MSG(proxy->InContainer() || triggered_zero_handles_,
                   "triggered_zero_handles_ is %d and proxy is %sin container\n",
                   triggered_zero_handles_, proxy->InContainer() ? "" : "not ");
  return proxy->InContainer() ? proxies_.erase(*proxy) : nullptr;
}

void PagerDispatcher::on_zero_handles() {
  Guard<Mutex> guard{&lock_};
  DEBUG_ASSERT(!triggered_zero_handles_);
  // Set triggered_zero_handles_ to true before starting to release proxies, so that a racy call to
  // PagerDispatcher::ReleaseProxy knows it's not incorrect to not find the proxy in the list.
  triggered_zero_handles_ = true;
  while (!proxies_.is_empty()) {
    fbl::RefPtr<PagerProxy> proxy = proxies_.pop_front();

    // Call unlocked to prevent a double-lock if PagerDispatcher::ReleaseProxy is called,
    // and to preserve the lock order that PagerProxy locks are acquired before the
    // list lock.
    guard.CallUnlocked([proxy = ktl::move(proxy)]() mutable { proxy->OnDispatcherClose(); });
  }
}

zx_status_t PagerDispatcher::RangeOp(uint32_t op, fbl::RefPtr<VmObject> vmo, uint64_t offset,
                                     uint64_t length, uint64_t data) {
  switch (op) {
    case ZX_PAGER_OP_FAIL: {
      auto signed_data = static_cast<int64_t>(data);
      if (signed_data < INT32_MIN || signed_data > INT32_MAX) {
        return ZX_ERR_INVALID_ARGS;
      }
      auto error_status = static_cast<zx_status_t>(data);
      if (!PageSource::IsValidExternalFailureCode(error_status)) {
        return ZX_ERR_INVALID_ARGS;
      }
      return vmo->FailPageRequests(offset, length, error_status);
    }
    case ZX_PAGER_OP_DIRTY: {
      if (data != 0) {
        return ZX_ERR_INVALID_ARGS;
      }
      return vmo->DirtyPages(offset, length);
    }
    case ZX_PAGER_OP_WRITEBACK_BEGIN: {
      if (data != 0) {
        return ZX_ERR_INVALID_ARGS;
      }
      return vmo->WritebackBegin(offset, length);
    }
    case ZX_PAGER_OP_WRITEBACK_END: {
      if (data != 0) {
        return ZX_ERR_INVALID_ARGS;
      }
      return vmo->WritebackEnd(offset, length);
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t PagerDispatcher::QueryDirtyRanges(VmAspace* current_aspace, fbl::RefPtr<VmObject> vmo,
                                              uint64_t offset, uint64_t length,
                                              user_out_ptr<void> buffer, size_t buffer_size,
                                              user_out_ptr<size_t> actual,
                                              user_out_ptr<size_t> avail) {
  // State captured by |copy_to_buffer| below.
  struct CopyToBufferInfo {
    // Index into |buffer|, used to populate its entries.
    size_t index = 0;
    // Total number of dirty ranges discovered.
    size_t total = 0;
    // Whether the total number of ranges need to be computed, depending on whether |avail| is
    // supplied.
    bool compute_total = false;
    // The range that enumeration runs over. Might get updated when enumeration ends early due to a
    // page fault.
    uint64_t offset = 0;
    uint64_t length = 0;
    // The buffer to copy out ranges to.
    user_out_ptr<zx_vmo_dirty_range_t> buffer = user_out_ptr<zx_vmo_dirty_range_t>(nullptr);
    size_t buffer_size = 0;
    // State that will get populated if the user copy in the DirtyRangeEnumerateFunction encounters
    // a page fault.
    vaddr_t pf_va = 0;
    uint pf_flags = 0;
    bool captured_fault_info = false;
  };
  CopyToBufferInfo info = {};
  info.offset = offset;
  info.length = length;
  info.buffer = buffer.reinterpret<zx_vmo_dirty_range_t>();
  info.buffer_size = buffer_size;
  if (avail) {
    info.compute_total = true;
  }

  // Enumeration function that will be invoked on each dirty range found.
  VmObject::DirtyRangeEnumerateFunction copy_to_buffer = [&info](uint64_t range_offset,
                                                                 uint64_t range_len) {
    // No more space in the buffer.
    if ((info.index + 1) * sizeof(zx_vmo_dirty_range_t) > info.buffer_size) {
      // If we were not asked to compute the total, we can end termination early as there is
      // nothing more to copy out.
      if (!info.compute_total) {
        return ZX_ERR_STOP;
      }
      // If there is no more space in the |buffer|, only update the total without trying to copy
      // out any more ranges.
      ++info.total;
      return ZX_ERR_NEXT;
    }

    zx_vmo_dirty_range_t dirty_range;
    memset(&dirty_range, 0, sizeof(dirty_range));
    dirty_range.offset = range_offset;
    dirty_range.length = range_len;
    dirty_range.options = 0u;

    UserCopyCaptureFaultsResult copy_result =
        info.buffer.element_offset(info.index).copy_to_user_capture_faults(dirty_range);
    // Stash fault information if a fault is encountered. Return early from enumeration with
    // ZX_ERR_SHOULD_WAIT so that the page fault can be resolved.
    if (copy_result.status != ZX_OK) {
      info.captured_fault_info = true;
      info.pf_va = copy_result.fault_info->pf_va;
      info.pf_flags = copy_result.fault_info->pf_flags;

      // Update the offset and length to skip over the range that we've already processed dirty
      // ranges for, to allow forward progress of the syscall.
      uint64_t processed = range_offset - info.offset;
      info.offset += processed;
      info.length -= processed;

      return ZX_ERR_SHOULD_WAIT;
    }
    // We were able to successfully copy out this dirty range. Advance the index and continue
    // with the enumeration.
    ++info.index;
    ++info.total;
    return ZX_ERR_NEXT;
  };

  // Enumerate dirty ranges with |copy_to_buffer|. If page faults are captured, resolve them and
  // retry enumeration.
  zx_status_t status = ZX_OK;
  do {
    status = vmo->EnumerateDirtyRanges(info.offset, info.length, ktl::move(copy_to_buffer));
    // Per |copy_to_buffer|, enumeration will terminate early with ZX_ERR_SHOULD_WAIT if a fault is
    // captured. Resolve the fault and then attempt the enumeration again.
    if (status == ZX_ERR_SHOULD_WAIT) {
      DEBUG_ASSERT(info.captured_fault_info);
      zx_status_t fault_status = current_aspace->SoftFault(info.pf_va, info.pf_flags);
      if (fault_status != ZX_OK) {
        return fault_status;
      }
      // Reset |captured_fault_info| so that a future page fault can set it again.
      info.captured_fault_info = false;
    } else if (status != ZX_OK) {
      // Another error was encountered. Return.
      return status;
    }
  } while (status == ZX_ERR_SHOULD_WAIT);

  DEBUG_ASSERT(status == ZX_OK);

  // Now try to copy out the total and actual number of ranges we populated in |buffer|. We don't
  // need to use copy_to_user_capture_faults() here; we don't hold any locks that need to be dropped
  // before handling a fault.
  if (actual) {
    status = actual.copy_to_user(info.index);
    if (status != ZX_OK) {
      return status;
    }
  }
  if (avail) {
    DEBUG_ASSERT(info.total >= info.index);
    status = avail.copy_to_user(info.total);
  }
  return status;
}
