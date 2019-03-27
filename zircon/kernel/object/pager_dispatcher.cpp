// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/pager_dispatcher.h>

#include <lib/counters.h>
#include <object/thread_dispatcher.h>
#include <trace.h>
#include <vm/page_source.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_pager_create_count, "dispatcher.pager.create")
KCOUNTER(dispatcher_pager_destroy_count, "dispatcher.pager.destroy")

zx_status_t PagerDispatcher::Create(fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) PagerDispatcher();
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    *rights = default_rights();
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

PagerDispatcher::PagerDispatcher() : SoloDispatcher() {
    kcounter_add(dispatcher_pager_create_count, 1);
}

PagerDispatcher::~PagerDispatcher() {
    DEBUG_ASSERT(srcs_.is_empty());
    kcounter_add(dispatcher_pager_destroy_count, 1);
}

zx_status_t PagerDispatcher::CreateSource(fbl::RefPtr<PortDispatcher> port,
                                          uint64_t key, fbl::RefPtr<PageSource>* src_out) {
    fbl::AllocChecker ac;
    auto src = fbl::AdoptRef(new (&ac) PagerSource(this, ktl::move(port), key));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    Guard<fbl::Mutex> guard{&list_mtx_};
    srcs_.push_front(src);
    *src_out = ktl::move(src);
    return ZX_OK;
}

fbl::RefPtr<PagerSource> PagerDispatcher::ReleaseSource(PagerSource* src) {
    Guard<fbl::Mutex> guard{&list_mtx_};
    return src->InContainer() ? srcs_.erase(*src) : nullptr;
}

void PagerDispatcher::on_zero_handles() {
    Guard<fbl::Mutex> guard{&list_mtx_};
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

PagerSource::PagerSource(PagerDispatcher* dispatcher,
                         fbl::RefPtr<PortDispatcher> port, uint64_t key)
    : PageSource(), pager_(dispatcher), port_(ktl::move(port)), key_(key) {
    LTRACEF("%p key %lx\n", this, key_);
}

PagerSource::~PagerSource() {
    LTRACEF("%p\n", this);
    DEBUG_ASSERT(closed_);
    DEBUG_ASSERT(!complete_pending_);
}

void PagerSource::GetPageAsync(page_request_t* request) {
    Guard<fbl::Mutex> guard{&mtx_};
    ASSERT(!closed_);

    QueueMessageLocked(request);
}

void PagerSource::QueueMessageLocked(page_request_t* request) {
    if (packet_busy_) {
        list_add_tail(&pending_requests_, &request->node);
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

    // We can treat ZX_ERR_BAD_STATE as if the packet was queued
    // but the pager service never responds.
    // TODO: Bypass the port's max queued packet count to prevent ZX_ERR_SHOULD_WAIT
    ASSERT(port_->Queue(&packet_, ZX_SIGNAL_NONE, 0) != ZX_ERR_SHOULD_WAIT);
}

void PagerSource::ClearAsyncRequest(page_request_t* request) {
    Guard<fbl::Mutex> guard{&mtx_};
    ASSERT(!closed_);

    if (request == active_request_) {
        // Condition on whether or not we actually cancel the packet, to make sure
        // we don't race with a call to PagerSource::Free.
        if (port_->CancelQueued(&packet_)) {
            OnPacketFreedLocked();
        }
    } else if (list_in_list(&request->node)) {
        list_delete(&request->node);
    }
}

void PagerSource::SwapRequest(page_request_t* old, page_request_t* new_req) {
    Guard<fbl::Mutex> guard{&mtx_};
    ASSERT(!closed_);

    if (list_in_list(&old->node)) {
        list_replace_node(&old->node, &new_req->node);
    } else if (old == active_request_) {
        active_request_ = new_req;
    }
}
void PagerSource::OnDetach() {
    Guard<fbl::Mutex> guard{&mtx_};
    ASSERT(!closed_);

    complete_pending_ = true;
    QueueMessageLocked(&complete_request_);
}

void PagerSource::OnClose() {
    fbl::RefPtr<PagerSource> self;

    Guard<fbl::Mutex> guard{&mtx_};
    ASSERT(!closed_);

    closed_ = true;
    if (!complete_pending_) {
        // We know PagerDispatcher::on_zero_handles hasn't been invoked, since that would
        // have already closed this pager source.
        self = pager_->ReleaseSource(this);
    } // else this is released in PagerSource::Free
}

void PagerSource::OnDispatcherClosed() {
    // The pager dispatcher's reference to this object is the only one we completely control. Now
    // that it's gone, we need to make sure that port_ doesn't end up with an invalid pointer
    // to packet_ if all external RefPtrs to this object go away.
    Guard<fbl::Mutex> guard{&mtx_};

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
            self_ref_ = fbl::WrapRefPtr(this);
        }
    } else {
        // Either the complete message had already been dispatched when this object was closed or
        // PagerSource::Free was called between this object being closed and this method taking the
        // lock. In either case, the port no longer has a reference and cleanup is already done.
    }
}

void PagerSource::Free(PortPacket* packet) {
    fbl::RefPtr<PagerSource> self;

    Guard<fbl::Mutex> guard{&mtx_};
    if (active_request_ != &complete_request_) {
        OnPacketFreedLocked();
    } else {
        complete_pending_ = false;
        if (closed_) {
            // If the source is closed, we need to do delayed cleanup. If the dispatcher
            // has already been torn down, then there's a self-reference we need to clean
            // up. Otherwise, clean up the dispatcher's reference to us.
            self = std::move(self_ref_);
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
        QueueMessageLocked(list_remove_head_type(&pending_requests_, page_request, node));
    }
}

zx_status_t PagerSource::WaitOnEvent(event_t* event) {
    ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::PAGER);
    return event_wait_deadline(event, ZX_TIME_INFINITE, true);
}
