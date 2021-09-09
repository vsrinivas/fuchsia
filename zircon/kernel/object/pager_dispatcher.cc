// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>
#include <trace.h>

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
  DEBUG_ASSERT(srcs_.is_empty());
  kcounter_add(dispatcher_pager_destroy_count, 1);
}

zx_status_t PagerDispatcher::CreateSource(fbl::RefPtr<PortDispatcher> port, uint64_t key,
                                          fbl::RefPtr<PageSource>* src_out) {
  Guard<Mutex> guard{&lock_};
  // Make sure on_zero_handles has not been called. This could happen if a call to pager_create_vmo
  // races with closing the last handle, as pager_create_vmo does not hold the handle table lock
  // over this operation.
  if (triggered_zero_handles_) {
    return ZX_ERR_BAD_STATE;
  }

  fbl::AllocChecker ac;
  auto proxy = ktl::unique_ptr<PagerProxy>(new (&ac) PagerProxy(this, ktl::move(port), key));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto proxy_ptr = proxy.get();

  auto src = fbl::AdoptRef(new (&ac) PageSource(ktl::move(proxy)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  proxy_ptr->page_source_ = src.get();

  srcs_.push_front(src);
  *src_out = ktl::move(src);
  return ZX_OK;
}

fbl::RefPtr<PageSource> PagerDispatcher::ReleaseSource(PageSource* src) {
  Guard<Mutex> guard{&lock_};
  // src might not be in the container since we could be racing with a call to on_zero_handles, but
  // that should only happen if we have triggered_zero_handles_. In particular the caller should not
  // be trying to release a source that it knows is not here.
  DEBUG_ASSERT(src->InContainer() != triggered_zero_handles_);
  return src->InContainer() ? srcs_.erase(*src) : nullptr;
}

void PagerDispatcher::on_zero_handles() {
  Guard<Mutex> guard{&lock_};
  DEBUG_ASSERT(!triggered_zero_handles_);
  triggered_zero_handles_ = true;
  while (!srcs_.is_empty()) {
    fbl::RefPtr<PageSource> src = srcs_.pop_front();

    // Call unlocked to prevent a double-lock if PagerDispatcher::ReleaseSource is called,
    // and to preserve the lock order that PagerProxy locks are acquired before the
    // list lock.
    guard.CallUnlocked([&src]() mutable { src->OnPageProviderDispatcherClose(); });
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
      if (!PageSource::IsValidFailureCode(error_status)) {
        return ZX_ERR_INVALID_ARGS;
      }
      return vmo->FailPageRequests(offset, length, error_status);
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}
