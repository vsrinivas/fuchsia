// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/pager_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <trace.h>
#include <vm/page_source.h>

#define LOCAL_TRACE 0

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

PagerDispatcher::PagerDispatcher() : SoloDispatcher() {}

PagerDispatcher::~PagerDispatcher() {}

zx_status_t PagerDispatcher::CreateSource(fbl::RefPtr<PortDispatcher> port,
                                          uint64_t key, fbl::RefPtr<PageSource>* src_out) {
    fbl::AllocChecker ac;
    auto wrapper = fbl::make_unique_checked<PageSourceWrapper>(&ac, this, ktl::move(port), key);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto src = fbl::AdoptRef(new (&ac) PageSource(wrapper.get(), get_koid()));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    fbl::AutoLock lock(&wrapper->mtx_);
    wrapper->src_ = src;

    fbl::AutoLock lock2(&mtx_);
    srcs_.push_front(ktl::move(wrapper));

    *src_out = ktl::move(src);
    return ZX_OK;
}

void PagerDispatcher::ReleaseSource(PageSourceWrapper* src) {
    fbl::AutoLock lock(&mtx_);
    srcs_.erase(*src);
}

void PagerDispatcher::on_zero_handles() {
    fbl::DoublyLinkedList<fbl::unique_ptr<PageSourceWrapper>> srcs;

    mtx_.Acquire();
    while (!srcs_.is_empty()) {
        auto& src = srcs_.front();
        fbl::RefPtr<PageSource> inner;
        {
            fbl::AutoLock lock(&src.mtx_);
            inner = src.src_;
        }

        // Call close outside of the lock, since it will call back into ::OnClose.
        mtx_.Release();
        if (inner) {
            inner->Close();
        }
        mtx_.Acquire();
    }
    mtx_.Release();
}

PageSourceWrapper::PageSourceWrapper(PagerDispatcher* dispatcher,
                                     fbl::RefPtr<PortDispatcher> port, uint64_t key)
    : pager_(dispatcher), port_(ktl::move(port)), key_(key) {
    LTRACEF("%p key %lx\n", this, key_);
}

PageSourceWrapper::~PageSourceWrapper() {
    LTRACEF("%p\n", this);
    DEBUG_ASSERT(closed_);
}

void PageSourceWrapper::GetPageAsync(page_request_t* request) {
    fbl::AutoLock lock(&mtx_);
    QueueMessageLocked(request);
}

void PageSourceWrapper::QueueMessageLocked(page_request_t* request) {
    if (packet_busy_) {
        list_add_tail(&pending_requests_, &request->node);
        return;
    }

    packet_busy_ = true;
    active_request_ = request;

    zx_port_packet_t packet = {
        .key = key_,
        .type = ZX_PKT_TYPE_PAGE_REQUEST,
        .status = ZX_OK,
        .page_request = {
            .command = ZX_PAGER_VMO_READ,
            .flags = 0,
            .reserved0 = 0,
            .offset = request->offset,
            .length = request->length,
            .reserved1 = 0,
        },
    };
    packet_.packet = packet;

    // We can treat ZX_ERR_BAD_STATE as if the packet was queued
    // but the pager service never responds.
    // TODO: Bypass the port's max queued packet count to prevent ZX_ERR_SHOULD_WAIT
    ASSERT(port_->Queue(&packet_, ZX_SIGNAL_NONE, 0) != ZX_ERR_SHOULD_WAIT);
}

void PageSourceWrapper::ClearAsyncRequest(page_request_t* request) {
    fbl::AutoLock lock(&mtx_);
    if (request == active_request_) {
        // Condition on whether or not we atually cancel the packet, to make sure
        // we don't race with a call to ::Free.
        if (port_->CancelQueued(&packet_)) {
            OnPacketFreedLocked();
        }
    } else if (list_in_list(&request->node)) {
        list_delete(&request->node);
    }
}

void PageSourceWrapper::SwapRequest(page_request_t* old, page_request_t* new_req) {
    fbl::AutoLock lock(&mtx_);
    if (list_in_list(&old->node)) {
        list_replace_node(&old->node, &new_req->node);
    } else if (old == active_request_) {
        active_request_ = new_req;
    }
}

void PageSourceWrapper::OnClose() {
    {
        fbl::AutoLock lock(&mtx_);
        closed_ = true;
    }
    pager_->ReleaseSource(this);
}

void PageSourceWrapper::Free(PortPacket* packet) {
    fbl::AutoLock lock(&mtx_);
    OnPacketFreedLocked();
}

void PageSourceWrapper::OnPacketFreedLocked() {
    packet_busy_ = false;
    active_request_ = nullptr;
    if (!list_is_empty(&pending_requests_)) {
        QueueMessageLocked(list_remove_head_type(&pending_requests_, page_request, node));
    }
}

zx_status_t PageSourceWrapper::WaitOnEvent(event_t* event) {
    ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::PAGER);
    return event_wait_deadline(event, ZX_TIME_INFINITE, true);
}
