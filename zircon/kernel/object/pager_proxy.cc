// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/counters.h>
#include <trace.h>

#include <lk/init.h>
#include <object/pager_dispatcher.h>
#include <object/pager_proxy.h>
#include <object/thread_dispatcher.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_pager_overtime_wait_count, "dispatcher.pager.overtime_waits")
KCOUNTER(dispatcher_pager_total_request_count, "dispatcher.pager.total_requests")
KCOUNTER(dispatcher_pager_succeeded_request_count, "dispatcher.pager.succeeded_requests")
KCOUNTER(dispatcher_pager_failed_request_count, "dispatcher.pager.failed_requests")
KCOUNTER(dispatcher_pager_timed_out_request_count, "dispatcher.pager.timed_out_requests")

PagerProxy::PagerProxy(PagerDispatcher* dispatcher, fbl::RefPtr<PortDispatcher> port, uint64_t key)
    : pager_(dispatcher), port_(ktl::move(port)), key_(key) {
  LTRACEF("%p key %lx\n", this, key_);
}

PagerProxy::~PagerProxy() {
  LTRACEF("%p\n", this);
  DEBUG_ASSERT(closed_);
  DEBUG_ASSERT(!complete_pending_);
}

void PagerProxy::GetPageAsync(page_request_t* request) {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  QueuePacketLocked(request);
}

void PagerProxy::QueuePacketLocked(page_request_t* request) {
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

    // Trace flow events require an enclosing duration.
    VM_KTRACE_DURATION(1, "page_request_queue", offset, length);
    VM_KTRACE_FLOW_BEGIN(1, "page_request_queue", reinterpret_cast<uintptr_t>(&packet_));
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

void PagerProxy::ClearAsyncRequest(page_request_t* request) {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  if (request == active_request_) {
    if (request != &complete_request_) {
      // Trace flow events require an enclosing duration.
      VM_KTRACE_DURATION(1, "page_request_queue", active_request_->offset, active_request_->length);
      VM_KTRACE_FLOW_END(1, "page_request_queue", reinterpret_cast<uintptr_t>(&packet_));
    }
    // Condition on whether or not we actually cancel the packet, to make sure
    // we don't race with a call to PagerProxy::Free.
    if (port_->CancelQueued(&packet_)) {
      OnPacketFreedLocked();
    }
  } else if (list_in_list(&request->provider_node)) {
    list_delete(&request->provider_node);
  }
}

void PagerProxy::SwapRequest(page_request_t* old, page_request_t* new_req) {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  if (list_in_list(&old->provider_node)) {
    list_replace_node(&old->provider_node, &new_req->provider_node);
  } else if (old == active_request_) {
    active_request_ = new_req;
  }
}

void PagerProxy::OnDetach() {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  complete_pending_ = true;
  QueuePacketLocked(&complete_request_);
}

void PagerProxy::OnClose() {
  fbl::RefPtr<PagerProxy> self_ref;
  fbl::RefPtr<PageSource> self_src;
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  closed_ = true;
  if (!complete_pending_) {
    // We know PagerDispatcher::on_zero_handles hasn't been invoked, since that would
    // have already closed this pager proxy via OnDispatcherClose. Therefore we are free to
    // immediately clean up.
    self_ref = pager_->ReleaseProxy(this);
    self_src = ktl::move(page_source_);
  } else {
    // There are still pending messages that we would like to wait to be received and so we do not
    // perform CancelQueued like OnDispatcherClose does. However, we must leave the reference to
    // ourselves in pager_ so that OnDispatcherClose (and the forced packet cancelling) can happen
    // if needed.
    // Otherwise final delayed cleanup will happen in ::Free
  }
}

void PagerProxy::OnDispatcherClose() {
  fbl::RefPtr<PageSource> self_src;
  // The pager dispatcher's reference to this object is the only one we completely control. Now
  // that it's gone, we need to make sure that port_ doesn't end up with an invalid pointer
  // to packet_ if all external RefPtrs to this object go away.
  Guard<Mutex> guard{&mtx_};

  if (!closed_) {
    // Close the page source from our end.
    DEBUG_ASSERT(page_source_);
    self_src = page_source_;
    // Call Close without the lock to
    //  * Not violate lock ordering
    //  * Allow it to call back into ::OnClose
    guard.CallUnlocked([&self_src]() mutable { self_src->Close(); });
  }

  // As the Pager dispatcher is going away, we are not content to keep these objects alive
  // indefinitely until messages are read, instead we want to cancel everything as soon as possible
  // to avoid memory leaks. Therefore we will attempt to cancel any queued final packet.
  if (complete_pending_) {
    if (port_->CancelQueued(&packet_)) {
      // We successfully cancelled the message, so we don't have to worry about
      // PagerProxy::Free being called, and can immediately break the refptr cycle.
      complete_pending_ = false;
    } else {
      // If we failed to cancel the message, then there is a pending call to PagerProxy::Free. It
      // will cleanup the RefPtr cycle, although only if closed_ is true, which should be the case
      // since we performed the Close step earlier.
      DEBUG_ASSERT(closed_);
    }
  } else {
    // Either the complete message had already been dispatched when this object was closed or
    // PagerProxy::Free was called between this object being closed and this method taking the
    // lock. In either case, the port no longer has a reference, any RefPtr cycles have been broken
    // and cleanup is already done.
    DEBUG_ASSERT(!page_source_);
  }
}

void PagerProxy::Free(PortPacket* packet) {
  fbl::RefPtr<PagerProxy> self_ref;
  fbl::RefPtr<PageSource> self_src;

  Guard<Mutex> guard{&mtx_};
  if (active_request_ != &complete_request_) {
    // Trace flow events require an enclosing duration.
    VM_KTRACE_DURATION(1, "page_request_queue", active_request_->offset, active_request_->length);
    VM_KTRACE_FLOW_END(1, "page_request_queue", reinterpret_cast<uintptr_t>(packet));
    OnPacketFreedLocked();
  } else {
    // Freeing the complete_request_ indicates we have completed a pending action that might have
    // been delaying cleanup.
    complete_pending_ = false;
    if (closed_) {
      // If the source is closed, we need to do delayed cleanup. Make sure we are not still in the
      // pagers proxy list, and then break our refptr cycle.
      DEBUG_ASSERT(page_source_);
      // self_ref could be a nullptr if we have ended up racing with pager dispatcher tear down.
      // This is fine as OnDispatcherClose will notice closed_ is true and complete_pending_ is true
      // and do no work.
      self_ref = pager_->ReleaseProxy(this);
      self_src = ktl::move(page_source_);
    }
  }
}

void PagerProxy::OnPacketFreedLocked() {
  packet_busy_ = false;
  active_request_ = nullptr;
  if (!list_is_empty(&pending_requests_)) {
    QueuePacketLocked(list_remove_head_type(&pending_requests_, page_request, provider_node));
  }
}

void PagerProxy::SetPageSourceUnchecked(fbl::RefPtr<PageSource> src) {
  // SetPagerSource is a private function and is only called by the PagerDispatcher just after
  // construction, unfortunately it needs to be called under the PagerDispatcher lock and lock
  // ordering is always PagerProxy->PagerDispatcher, and so we cannot acquire the lock here.
  auto func = [this, &src]() TA_NO_THREAD_SAFETY_ANALYSIS { page_source_ = ktl::move(src); };
  func();
}

zx_status_t PagerProxy::WaitOnEvent(Event* event) {
  ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::PAGER);
  kcounter_add(dispatcher_pager_total_request_count, 1);
  uint32_t waited = 0;
  // declare a lambda to calculate our deadline to avoid an excessively large statement in our
  // loop condition.
  auto make_deadline = []() {
    if (gBootOptions->userpager_overtime_wait_seconds == 0) {
      return Deadline::infinite();
    } else {
      return Deadline::after(ZX_SEC(gBootOptions->userpager_overtime_wait_seconds));
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
    if (gBootOptions->userpager_overtime_timeout_seconds > 0 &&
        waited * gBootOptions->userpager_overtime_wait_seconds >=
            gBootOptions->userpager_overtime_timeout_seconds) {
      Guard<Mutex> guard{&mtx_};
      printf("ERROR Page source %p has been blocked for %" PRIu64
             " seconds. Page request timed out.\n",
             page_source_.get(), gBootOptions->userpager_overtime_timeout_seconds);
      dump_thread(Thread::Current::Get(), false);
      kcounter_add(dispatcher_pager_timed_out_request_count, 1);
      return ZX_ERR_TIMED_OUT;
    }

    // Determine whether we have any requests that have not yet been received off of the port.
    fbl::RefPtr<PageSource> src;
    bool active;
    {
      Guard<Mutex> guard{&mtx_};
      active = !!active_request_;
      src = page_source_;
    }
    printf("WARNING Page source %p has been blocked for %" PRIu64
           " seconds with%s message waiting on port.\n",
           src.get(), waited * gBootOptions->userpager_overtime_wait_seconds, active ? "" : " no");
    // Dump out the rest of the state of the oustanding requests.
    if (src) {
      src->Dump();
    }
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

void PagerProxy::Dump() {
  Guard<Mutex> guard{&mtx_};
  printf(
      "pager_proxy %p pager_dispatcher %p page_source %p key %lu\n"
      "  closed %d packet_busy %d complete_pending %d\n",
      this, pager_, page_source_.get(), key_, closed_, packet_busy_, complete_pending_);

  if (active_request_) {
    printf("  active request on pager port [0x%lx, 0x%lx)\n", active_request_->offset,
           active_request_->length);
  } else {
    printf("  no active request on pager port\n");
  }

  page_request_t* req;
  list_for_every_entry (&pending_requests_, req, page_request_t, provider_node) {
    printf("  pending req to queue on pager port [0x%lx, 0x%lx)\n", req->offset, req->length);
  }
}
