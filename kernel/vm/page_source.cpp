// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/auto_lock.h>
#include <kernel/lockdep.h>
#include <ktl/move.h>
#include <trace.h>
#include <vm/page_source.h>

#define LOCAL_TRACE 0

PageSource::PageSource() {
    LTRACEF("%p\n", this);
}

PageSource::~PageSource() {
    LTRACEF("%p\n", this);
    DEBUG_ASSERT(detached_);
    DEBUG_ASSERT(closed_);
}

void PageSource::Detach() {
    canary_.Assert();
    LTRACEF("%p\n", this);
    Guard<fbl::Mutex> guard{&page_source_mtx_};
    if (detached_) {
        return;
    }

    detached_ = true;

    // Cancel read requests (which everything for now)
    while (!outstanding_requests_.is_empty()) {
        auto req = outstanding_requests_.pop_front();
        LTRACEF("dropping request with offset %lx\n", req->offset_);

        // Tell the clients the request is complete - they'll fail when they try
        // asking for the requested pages again after failing to find the pages
        // for this request.
        CompleteRequestLocked(req);
    }
    OnDetach();
}


void PageSource::Close() {
    canary_.Assert();
    LTRACEF("%p\n", this);
    // TODO: Close will have more meaning once writeback is implemented
    Detach();

    Guard<fbl::Mutex> guard{&page_source_mtx_};
    if (!closed_) {
        closed_ = true;
        OnClose();
    }
}

void PageSource::OnPagesSupplied(uint64_t offset, uint64_t len) {
    canary_.Assert();
    LTRACEF("%p offset %lx, len %lx\n", this, offset, len);
    uint64_t end = offset + len;

    Guard<fbl::Mutex> guard{&page_source_mtx_};
    if (detached_) {
        return;
    }

    // The first possible request we could fulfill is the one with the smallest
    // address that is at least offset. Then keep looking as long as the target's
    // request's start offset is less than the supply end.
    auto start = outstanding_requests_.lower_bound(offset);
    while (start.IsValid() && start->offset_ < end) {
        auto cur = start;
        ++start;

        LTRACEF("%p, signaling %lx\n", this, cur->offset_);

        // Notify anything waiting on this page
        CompleteRequestLocked(outstanding_requests_.erase(cur));
    }
}

zx_status_t PageSource::GetPage(uint64_t offset, PageRequest* request,
                                vm_page_t** const page_out, paddr_t* const pa_out) {
    canary_.Assert();
    ASSERT(request);

    Guard<fbl::Mutex> guard{&page_source_mtx_};
    LTRACEF("%p offset %lx, %lu\n", this, offset, get_current_thread()->user_tid);
    if (detached_) {
        return ZX_ERR_NOT_FOUND;
    }

    if (GetPage(offset, page_out, pa_out)) {
        return ZX_OK;
    }

    request->Init(fbl::RefPtr<PageSource>(this), offset);

    LTRACEF("%p %p\n", this, request);
    auto overlap = outstanding_requests_.find(request->offset_);
    if (overlap.IsValid()) {
        overlap->overlap_.push_back(request);
    } else {
        list_clear_node(&request->read_request_.node);
        request->read_request_.offset = request->offset_;
        request->read_request_.length = PAGE_SIZE;

        GetPageAsync(&request->read_request_);
        outstanding_requests_.insert(request);
    }

    return ZX_ERR_SHOULD_WAIT;
}

void PageSource::CompleteRequestLocked(PageRequest* req) {
    // Take the request back from the callback before waking
    // up the corresponding thread.
    ClearAsyncRequest(&req->read_request_);

    while (!req->overlap_.is_empty()) {
        auto waiter = req->overlap_.pop_front();
        waiter->offset_ = UINT64_MAX;
        event_signal(&waiter->event_, true);
    }
    req->offset_ = UINT64_MAX;
    event_signal(&req->event_, true);
}

void PageSource::CancelRequest(PageRequest* request) {
    canary_.Assert();
    Guard<fbl::Mutex> guard{&page_source_mtx_};
    LTRACEF("%p %lx\n", this, request->offset_);

    if (request->offset_ == UINT64_MAX) {
        return;
    }

    if (static_cast<fbl::DoublyLinkedListable<PageRequest*>*>(request)->InContainer()) {
        LTRACEF("Overlap node\n");
        // This node is overlapping some other node, so just remove the request
        auto main_node = outstanding_requests_.find(request->offset_);
        main_node->overlap_.erase(*request);
    } else if (!request->overlap_.is_empty()) {
        LTRACEF("Outstanding with overlap\n");
        // This node is an outstanding request with overlap, so replace it with the
        // first overlap node.
        auto new_node = request->overlap_.pop_front();

        new_node->overlap_.swap(request->overlap_);
        new_node->offset_ = request->offset_;

        list_clear_node(&new_node->read_request_.node);
        new_node->read_request_.offset = request->offset_;
        new_node->read_request_.length = PAGE_SIZE;

        outstanding_requests_.erase(*request);
        outstanding_requests_.insert(new_node);

        SwapRequest(&request->read_request_, &new_node->read_request_);
    } else if (static_cast<fbl::WAVLTreeContainable<PageRequest*>*>(request)->InContainer()) {
        LTRACEF("Outstanding no overlap\n");
        // This node is an outstanding request with no overlap
        outstanding_requests_.erase(*request);
        ClearAsyncRequest(&request->read_request_);
    }

    request->offset_ = UINT64_MAX;
}

PageRequest::~PageRequest() {
    if (offset_ != UINT64_MAX) {
        src_->CancelRequest(this);
    }
}

void PageRequest::Init(fbl::RefPtr<PageSource> src, uint64_t offset) {
    DEBUG_ASSERT(offset_ == UINT64_MAX);
    offset_ = offset;
    src_ = ktl::move(src);
    event_ = EVENT_INITIAL_VALUE(event_, 0, EVENT_FLAG_AUTOUNSIGNAL);
}

zx_status_t PageRequest::Wait() {
    zx_status_t status = src_->WaitOnEvent(&event_);
    if (status != ZX_OK) {
        src_->CancelRequest(this);
    }
    return status;
}
