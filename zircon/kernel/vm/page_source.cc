// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/console.h>
#include <trace.h>

#include <fbl/auto_lock.h>
#include <kernel/lockdep.h>
#include <ktl/move.h>
#include <vm/page_source.h>

#define LOCAL_TRACE 0

PageSource::PageSource() { LTRACEF("%p\n", this); }

PageSource::~PageSource() {
  LTRACEF("%p\n", this);
  DEBUG_ASSERT(detached_);
  DEBUG_ASSERT(closed_);
}

void PageSource::Detach() {
  canary_.Assert();
  LTRACEF("%p\n", this);
  Guard<Mutex> guard{&page_source_mtx_};
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

  Guard<Mutex> guard{&page_source_mtx_};
  if (!closed_) {
    closed_ = true;
    OnClose();
  }
}

void PageSource::OnPagesSupplied(uint64_t offset, uint64_t len) {
  canary_.Assert();
  LTRACEF_LEVEL(2, "%p offset %lx, len %lx\n", this, offset, len);
  uint64_t end;
  bool overflow = add_overflow(offset, len, &end);
  DEBUG_ASSERT(!overflow);  // vmobject should have already validated overflow

  Guard<Mutex> guard{&page_source_mtx_};
  if (detached_) {
    return;
  }

  // The first possible request we could fulfill is the one with the smallest
  // end address that is greater than offset. Then keep looking as long as the
  // target request's start offset is less than the supply end.
  auto start = outstanding_requests_.upper_bound(offset);
  while (start.IsValid() && start->offset_ < end) {
    auto cur = start;
    ++start;

    // Calculate how many pages were supplied to this request by finding the start and
    // end offsets of the operation in this request.
    uint64_t req_offset, req_end;
    if (offset >= cur->offset_) {
      // The operation started partway into this request.
      req_offset = offset - cur->offset_;
    } else {
      // The operation started before this request.
      req_offset = 0;
    }
    if (end < cur->GetEnd()) {
      // The operation ended partway into this request.
      req_end = end - cur->offset_;

      uint64_t unused;
      DEBUG_ASSERT(!sub_overflow(end, cur->offset_, &unused));
    } else {
      // The operation ended past the end of this request.
      req_end = cur->len_;
    }

    DEBUG_ASSERT(req_end >= req_offset);
    uint64_t fulfill = req_end - req_offset;

    // If we're not done, continue to the next request
    if (fulfill < cur->pending_size_) {
      cur->pending_size_ -= fulfill;
      continue;
    } else if (fulfill > cur->pending_size_) {
      // This just means that part of the request was decommitted. That's not
      // an error, but it's good to know when we're tracing.
      LTRACEF("%p, excessive page count\n", this);
    }

    LTRACEF_LEVEL(2, "%p, signaling %lx\n", this, cur->offset_);

    // Notify anything waiting on this page
    CompleteRequestLocked(outstanding_requests_.erase(cur));
  }
}

void PageSource::OnPagesFailed(uint64_t offset, uint64_t len, zx_status_t error_status) {
  canary_.Assert();
  LTRACEF_LEVEL(2, "%p offset %lx, len %lx\n", this, offset, len);

  DEBUG_ASSERT(IsValidFailureCode(error_status));

  uint64_t end;
  bool overflow = add_overflow(offset, len, &end);
  DEBUG_ASSERT(!overflow);  // vmobject should have already validated overflow

  Guard<Mutex> guard{&page_source_mtx_};
  if (detached_) {
    return;
  }

  // The first possible request we could fail is the one with the smallest
  // end address that is greater than offset. Then keep looking as long as the
  // target request's start offset is less than the supply end.
  auto start = outstanding_requests_.upper_bound(offset);
  while (start.IsValid() && start->offset_ < end) {
    auto cur = start;
    ++start;

    LTRACEF_LEVEL(2, "%p, signaling failure %d %lx\n", this, error_status, cur->offset_);

    // Notify anything waiting on this page.
    CompleteRequestLocked(outstanding_requests_.erase(cur), error_status);
  }
}

// static
bool PageSource::IsValidFailureCode(zx_status_t error_status) {
  switch (error_status) {
    case ZX_ERR_IO:
    case ZX_ERR_IO_DATA_INTEGRITY:
    case ZX_ERR_BAD_STATE:
      return true;
    default:
      return false;
  }
}

zx_status_t PageSource::GetPage(uint64_t offset, PageRequest* request, VmoDebugInfo vmo_debug_info,
                                vm_page_t** const page_out, paddr_t* const pa_out) {
  canary_.Assert();
  ASSERT(request);
  offset = fbl::round_down(offset, static_cast<uint64_t>(PAGE_SIZE));

  Guard<Mutex> guard{&page_source_mtx_};
  if (detached_) {
    return ZX_ERR_NOT_FOUND;
  }

  if (GetPage(offset, vmo_debug_info, page_out, pa_out)) {
    return ZX_OK;
  }

  // Check if request is initialized and initialize it if it isn't (it can be initialized
  // for batch requests).
  if (request->offset_ == UINT64_MAX) {
    request->Init(fbl::RefPtr<PageSource>(this), offset, vmo_debug_info);
    LTRACEF_LEVEL(2, "%p offset %lx\n", this, offset);
  }

#ifdef DEBUG_ASSERT_IMPLEMENTED
  ASSERT(current_request_ == nullptr || current_request_ == request);
  current_request_ = request;
#endif  // DEBUG_ASSERT_IMPLEMENTED

  bool send_request = false;
  zx_status_t res;
  if (request->allow_batching_) {
    // If possible, append the page directly to the current page. Else have the
    // caller try again with a new request.
    if (request->offset_ + request->len_ == offset) {
      request->len_ += PAGE_SIZE;

      // Assert on overflow, since it means vmobject is trying to get out-of-bounds pages.
      uint64_t unused;
      DEBUG_ASSERT(request->len_ >= PAGE_SIZE);
      DEBUG_ASSERT(!add_overflow(request->offset_, request->len_, &unused));

      bool end_batch = false;
      auto node = outstanding_requests_.upper_bound(request->offset_);
      if (node.IsValid()) {
        uint64_t cur_end = request->offset_ + request->len_;
        if (node->offset_ <= request->offset_) {
          // If offset is in [node->GetOffset(), node->GetEnd()), then we end
          // the batch when we'd stop overlapping.
          end_batch = node->GetEnd() == cur_end;
        } else {
          // If offset is less than node->GetOffset(), then we end the batch
          // when we'd start overlapping.
          end_batch = node->offset_ == cur_end;
        }
      }

      if (end_batch) {
        send_request = true;
        res = ZX_ERR_SHOULD_WAIT;
      } else {
        res = ZX_ERR_NEXT;
      }
    } else {
      send_request = true;
      res = ZX_ERR_SHOULD_WAIT;
    }
  } else {
    request->len_ = PAGE_SIZE;
    send_request = true;
    res = ZX_ERR_SHOULD_WAIT;
  }

  if (send_request) {
    RaiseReadRequestLocked(request);
  }

  return res;
}

zx_status_t PageSource::FinalizeRequest(PageRequest* request) {
  LTRACEF_LEVEL(2, "%p\n", this);
  DEBUG_ASSERT(request->offset_ != UINT64_MAX);

  Guard<Mutex> guard{&page_source_mtx_};
  if (detached_) {
    return ZX_ERR_NOT_FOUND;
  }

  RaiseReadRequestLocked(request);
  return ZX_ERR_SHOULD_WAIT;
}

void PageSource::RaiseReadRequestLocked(PageRequest* request) {
  LTRACEF_LEVEL(2, "%p %p\n", this, request);
  // Find the node with the smallest endpoint greater than offset and then
  // check to see if offset falls within that node.
  auto overlap = outstanding_requests_.upper_bound(request->offset_);
  if (overlap.IsValid() && overlap->offset_ <= request->offset_) {
    // GetPage guarantees that if offset lies in an existing node, then it is
    // completely contained in that node.
    overlap->overlap_.push_back(request);
  } else {
    request->pending_size_ = request->len_;

    list_clear_node(&request->read_request_.provider_node);
    request->read_request_.offset = request->offset_;
    request->read_request_.length = request->len_;

    GetPageAsync(&request->read_request_);
    outstanding_requests_.insert(request);
  }
#ifdef DEBUG_ASSERT_IMPLEMENTED
  current_request_ = nullptr;
#endif  // DEBUG_ASSERT_IMPLEMENTED
}

void PageSource::CompleteRequestLocked(PageRequest* req, zx_status_t status) {
  VM_KTRACE_DURATION(1, "page_request_complete", req->offset_, req->len_);

  // Take the request back from the callback before waking
  // up the corresponding thread.
  ClearAsyncRequest(&req->read_request_);

  while (!req->overlap_.is_empty()) {
    auto waiter = req->overlap_.pop_front();
    VM_KTRACE_FLOW_BEGIN(1, "page_request_signal", reinterpret_cast<uintptr_t>(waiter));
    waiter->offset_ = UINT64_MAX;
    waiter->event_.Signal(status);
  }
  VM_KTRACE_FLOW_BEGIN(1, "page_request_signal", reinterpret_cast<uintptr_t>(req));
  req->offset_ = UINT64_MAX;
  req->event_.Signal(status);
}

void PageSource::CancelRequest(PageRequest* request) {
  canary_.Assert();
  Guard<Mutex> guard{&page_source_mtx_};
  LTRACEF("%p %lx\n", this, request->offset_);

  if (request->offset_ == UINT64_MAX) {
    return;
  }

  if (static_cast<fbl::DoublyLinkedListable<PageRequest*>*>(request)->InContainer()) {
    LTRACEF("Overlap node\n");
    // This node is overlapping some other node, so just remove the request
    auto main_node = outstanding_requests_.upper_bound(request->offset_);
    ASSERT(main_node.IsValid());
    main_node->overlap_.erase(*request);
  } else if (!request->overlap_.is_empty()) {
    LTRACEF("Outstanding with overlap\n");
    // This node is an outstanding request with overlap, so replace it with the
    // first overlap node.
    auto new_node = request->overlap_.pop_front();

    new_node->overlap_.swap(request->overlap_);
    new_node->offset_ = request->offset_;
    new_node->len_ = request->len_;
    new_node->pending_size_ = request->pending_size_;

    list_clear_node(&new_node->read_request_.provider_node);
    new_node->read_request_.offset = request->offset_;
    new_node->read_request_.length = request->len_;

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

void PageSource::Dump() const {
  Guard<Mutex> guard{&page_source_mtx_};
  printf("page_source %p detached %d closed %d\n", this, detached_, closed_);
  for (auto& req : outstanding_requests_) {
    printf("  vmo 0x%lx/k%lu req [%lx, %lx) pending %lx overlap %lu\n", req.vmo_debug_info_.vmo_ptr,
           req.vmo_debug_info_.vmo_id, req.offset_, req.GetEnd(), req.pending_size_,
           req.overlap_.size_slow());
  }
}

PageRequest::~PageRequest() {
  if (offset_ != UINT64_MAX) {
    src_->CancelRequest(this);
  }
}

void PageRequest::Init(fbl::RefPtr<PageSource> src, uint64_t offset, VmoDebugInfo vmo_debug_info) {
  DEBUG_ASSERT(offset_ == UINT64_MAX);
  vmo_debug_info_ = vmo_debug_info;
  len_ = 0;
  offset_ = offset;
  src_ = ktl::move(src);

  event_.Unsignal();
}

zx_status_t PageRequest::Wait() {
  VM_KTRACE_DURATION(1, "page_request_wait", offset_, len_);
  zx_status_t status = src_->WaitOnEvent(&event_);
  VM_KTRACE_FLOW_END(1, "page_request_signal", reinterpret_cast<uintptr_t>(this));
  if (status != ZX_OK && !PageSource::IsValidFailureCode(status)) {
    src_->CancelRequest(this);
  }
  return status;
}

static int cmd_page_source(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
  notenoughargs:
    printf("not enough arguments\n");
  usage:
    printf("usage:\n");
    printf("%s dump <address>\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (!strcmp(argv[1].str, "dump")) {
    if (argc < 3) {
      goto notenoughargs;
    }
    reinterpret_cast<PageSource*>(argv[2].u)->Dump();
  } else {
    printf("unknown command\n");
    goto usage;
  }

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("vm_page_source", "page source debug commands", &cmd_page_source)
STATIC_COMMAND_END(ps_object)
