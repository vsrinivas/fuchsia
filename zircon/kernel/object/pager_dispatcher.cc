// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cmdline.h>
#include <lib/counters.h>
#include <trace.h>

#include <kernel/thread.h>
#include <lk/init.h>
#include <object/pager_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <vm/page_source.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_pager_create_count, "dispatcher.pager.create")
KCOUNTER(dispatcher_pager_destroy_count, "dispatcher.pager.destroy")
KCOUNTER(dispatcher_pager_overtime_wait_count, "dispatcher.pager.overtime_waits")
KCOUNTER(dispatcher_pager_total_request_count, "dispatcher.pager.total_requests")
KCOUNTER(dispatcher_pager_succeeded_request_count, "dispatcher.pager.succeeded_requests")
KCOUNTER(dispatcher_pager_failed_request_count, "dispatcher.pager.failed_requests")
KCOUNTER(dispatcher_pager_timed_out_request_count, "dispatcher.pager.timed_out_requests")

// Log warnings every |pager_overtime_wait_seconds| a thread is blocked waiting on a page
// request. If the thread has been waiting for |pager_overtime_timeout_seconds|, return an
// error instead of waiting indefinitely.
static constexpr uint64_t kDefaultPagerOvertimeWaitSeconds = 20;
static constexpr uint64_t kDefaultPagerOvertimeTimeoutSeconds = 300;
static uint64_t pager_overtime_wait_seconds = kDefaultPagerOvertimeWaitSeconds;
static uint64_t pager_overtime_timeout_seconds = kDefaultPagerOvertimeTimeoutSeconds;

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
  fbl::AllocChecker ac;
  auto src = fbl::AdoptRef(new (&ac) PagerSource(this, ktl::move(port), key));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  Guard<Mutex> guard{&list_mtx_};
  srcs_.push_front(src);
  *src_out = ktl::move(src);
  return ZX_OK;
}

fbl::RefPtr<PagerSource> PagerDispatcher::ReleaseSource(PagerSource* src) {
  Guard<Mutex> guard{&list_mtx_};
  return src->InContainer() ? srcs_.erase(*src) : nullptr;
}

void PagerDispatcher::on_zero_handles() {
  Guard<Mutex> guard{&list_mtx_};
  while (!srcs_.is_empty()) {
    fbl::RefPtr<PagerSource> src = srcs_.pop_front();

    // Call unlocked to prevent a double-lock if PagerDispatcher::ReleaseSource is called,
    // and to preserve the lock order that PagerSource locks are acquired before the
    // list lock.
    guard.CallUnlocked([&src]() mutable {
      src->Close();
      src->OnDispatcherClosed();
    });
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

PagerSource::PagerSource(PagerDispatcher* dispatcher, fbl::RefPtr<PortDispatcher> port,
                         uint64_t key)
    : PageSource(), pager_(dispatcher), port_(ktl::move(port)), key_(key) {
  LTRACEF("%p key %lx\n", this, key_);
}

PagerSource::~PagerSource() {
  LTRACEF("%p\n", this);
  DEBUG_ASSERT(closed_);
  DEBUG_ASSERT(!complete_pending_);
}

void PagerSource::GetPageAsync(page_request_t* request) {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  QueueMessageLocked(request);
}

void PagerSource::QueueMessageLocked(page_request_t* request) {
  if (packet_busy_) {
    list_add_tail(&pending_requests_, &request->provider_node);
    return;
  }

  packet_busy_ = true;
  active_request_ = request;

  uint64_t offset, length;
  uint16_t cmd;
  if (request != &complete_request_) {
    cmd = ZX_PAGER_VMO_READ;
    offset = request->offset;
    length = request->length;

    // The vm subsystem should guarantee this
    uint64_t unused;
    DEBUG_ASSERT(!add_overflow(offset, length, &unused));
  } else {
    offset = length = 0;
    cmd = ZX_PAGER_VMO_COMPLETE;
  }

  zx_port_packet_t packet = {};
  packet.key = key_;
  packet.type = ZX_PKT_TYPE_PAGE_REQUEST;
  packet.page_request.command = cmd;
  packet.page_request.offset = offset;
  packet.page_request.length = length;

  packet_.packet = packet;

  // We can treat ZX_ERR_BAD_HANDLE as if the packet was queued
  // but the pager service never responds.
  // TODO: Bypass the port's max queued packet count to prevent ZX_ERR_SHOULD_WAIT
  ASSERT(port_->Queue(&packet_, ZX_SIGNAL_NONE) != ZX_ERR_SHOULD_WAIT);
}

void PagerSource::ClearAsyncRequest(page_request_t* request) {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  if (request == active_request_) {
    // Condition on whether or not we actually cancel the packet, to make sure
    // we don't race with a call to PagerSource::Free.
    if (port_->CancelQueued(&packet_)) {
      OnPacketFreedLocked();
    }
  } else if (list_in_list(&request->provider_node)) {
    list_delete(&request->provider_node);
  }
}

void PagerSource::SwapRequest(page_request_t* old, page_request_t* new_req) {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  if (list_in_list(&old->provider_node)) {
    list_replace_node(&old->provider_node, &new_req->provider_node);
  } else if (old == active_request_) {
    active_request_ = new_req;
  }
}

void PagerSource::OnDetach() {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  complete_pending_ = true;
  QueueMessageLocked(&complete_request_);
}

void PagerSource::OnClose() {
  fbl::RefPtr<PagerSource> self;

  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  closed_ = true;
  if (!complete_pending_) {
    // We know PagerDispatcher::on_zero_handles hasn't been invoked, since that would
    // have already closed this pager source.
    self = pager_->ReleaseSource(this);
  }  // else this is released in PagerSource::Free
}

void PagerSource::OnDispatcherClosed() {
  // The pager dispatcher's reference to this object is the only one we completely control. Now
  // that it's gone, we need to make sure that port_ doesn't end up with an invalid pointer
  // to packet_ if all external RefPtrs to this object go away.
  Guard<Mutex> guard{&mtx_};

  if (complete_pending_) {
    if (port_->CancelQueued(&packet_)) {
      // We successfully cancelled the message, so we don't have to worry about
      // PagerSource::Free being called.
      complete_pending_ = false;
    } else {
      // If we failed to cancel the message, then there is a pending call to
      // PagerSource::Free. We need to make sure the object isn't deleted too early,
      // so have it keep a reference to itself, which PagerSource::Free will then
      // clean up.
      self_ref_ = fbl::RefPtr(this);
    }
  } else {
    // Either the complete message had already been dispatched when this object was closed or
    // PagerSource::Free was called between this object being closed and this method taking the
    // lock. In either case, the port no longer has a reference and cleanup is already done.
  }
}

void PagerSource::Free(PortPacket* packet) {
  fbl::RefPtr<PagerSource> self;

  Guard<Mutex> guard{&mtx_};
  if (active_request_ != &complete_request_) {
    OnPacketFreedLocked();
  } else {
    complete_pending_ = false;
    if (closed_) {
      // If the source is closed, we need to do delayed cleanup. If the dispatcher
      // has already been torn down, then there's a self-reference we need to clean
      // up. Otherwise, clean up the dispatcher's reference to us.
      self = ktl::move(self_ref_);
      if (!self) {
        self = pager_->ReleaseSource(this);
      }
    }
  }
}

void PagerSource::OnPacketFreedLocked() {
  packet_busy_ = false;
  active_request_ = nullptr;
  if (!list_is_empty(&pending_requests_)) {
    QueueMessageLocked(list_remove_head_type(&pending_requests_, page_request, provider_node));
  }
}

zx_status_t PagerSource::WaitOnEvent(Event* event) {
  ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::PAGER);
  kcounter_add(dispatcher_pager_total_request_count, 1);
  uint32_t waited = 0;
  // declare a lambda to calculate our deadline to avoid an excessively large statement in our
  // loop condition.
  auto make_deadline = []() {
    if (pager_overtime_wait_seconds == 0) {
      return Deadline::infinite();
    } else {
      return Deadline::after(ZX_SEC(pager_overtime_wait_seconds));
    }
  };
  zx_status_t result;
  while ((result = event->Wait(make_deadline())) == ZX_ERR_TIMED_OUT) {
    waited++;
    // We might trigger this loop multiple times as we exceed multiples of the overtime counter, but
    // we only want to count each unique overtime event in the kcounter.
    if (waited == 1) {
      dispatcher_pager_overtime_wait_count.Add(1);
    }

    // Error out if we've been waiting for longer than the specified timeout, to allow the rest of
    // the system to make progress (if possible).
    if (pager_overtime_timeout_seconds > 0 &&
        waited * pager_overtime_wait_seconds >= pager_overtime_timeout_seconds) {
      printf("ERROR Pager source %p has been blocked for %" PRIu64
             " seconds. Page request timed out.\n",
             this, pager_overtime_timeout_seconds);
      dump_thread(Thread::Current::Get(), false);
      kcounter_add(dispatcher_pager_timed_out_request_count, 1);
      return ZX_ERR_TIMED_OUT;
    }

    // Determine whether we have any requests that have not yet been received off of the port.
    bool active;
    {
      Guard<Mutex> guard{&mtx_};
      active = !!active_request_;
    }
    printf("WARNING pager source %p has been blocked for %" PRIu64
           " seconds with%s message waiting on port.\n",
           this, waited * pager_overtime_wait_seconds, active ? "" : " no");
    // Dump out the rest of the state of the oustanding requests.
    Dump();
  }

  if (result == ZX_OK) {
    kcounter_add(dispatcher_pager_succeeded_request_count, 1);
  } else {
    // Only counts failures that are *not* pager timeouts. Timeouts are tracked with
    // dispatcher_pager_timed_out_request_count, which is updated above when we
    // return early with ZX_ERR_TIMED_OUT.
    kcounter_add(dispatcher_pager_failed_request_count, 1);
  }

  return result;
}

static void pager_init_func(uint level) {
  pager_overtime_wait_seconds = gCmdline.GetUInt64("kernel.userpager.overtime_wait_seconds",
                                                   kDefaultPagerOvertimeWaitSeconds);
  pager_overtime_timeout_seconds = gCmdline.GetUInt64("kernel.userpager.overtime_timeout_seconds",
                                                      kDefaultPagerOvertimeTimeoutSeconds);
}

LK_INIT_HOOK(pager_init, &pager_init_func, LK_INIT_LEVEL_LAST)
