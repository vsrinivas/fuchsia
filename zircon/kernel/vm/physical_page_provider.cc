// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>
#include <trace.h>

#include <lib/fitx/result.h>
#include "include/vm/pmm.h"
#include "include/vm/vm_page_list.h"
#include "include/vm/physical_page_provider.h"
#include <object/thread_dispatcher.h>

#define LOCAL_TRACE 0

KCOUNTER(physical_reclaim_total_requests, "physical.reclaim.total_requests")
KCOUNTER(physical_reclaim_succeeded_requests, "physical.reclaim.succeeded_requests")
KCOUNTER(physical_reclaim_failed_requests, "physical.reclaim.failed_requests")

PhysicalPageProvider::PhysicalPageProvider(VmCowPages* cow_pages, paddr_t phys_base) : cow_pages_(cow_pages), phys_base_(phys_base) {
  LTRACEF("%p cow_pages: %p phys_base 0x%" PRIx64 "\n", this, cow_pages_, phys_base_);
}

PhysicalPageProvider::~PhysicalPageProvider() {
  LTRACEF("%p\n", this);
  DEBUG_ASSERT(detached_);
  DEBUG_ASSERT(closed_);
}

bool PhysicalPageProvider::GetPageSync(uint64_t offset, VmoDebugInfo vmo_debug_info,
                                       vm_page_t** const page_out, paddr_t* const pa_out) {
  return false;
}

// Called under lock of contiguous VMO that needs the pages.  The request is later processed at the
// start of WaitOnEvent.
void PhysicalPageProvider::GetPageAsync(page_request_t* request) {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  // At this point the page may be free (but not in free_loaned_list_), or in use by a different
  // VMO.
  //
  // Allocation of a new page to a VMO has an interval during which the page is not free, but also
  // isn't state == OBJECT yet.  During processing we rely on that interval occurring only under the
  // other VMO's lock, but we can't acquire the other VMO's lock here.
  QueueRequestLocked(request);
}

void PhysicalPageProvider::QueueRequestLocked(page_request_t* request) {
  ASSERT(!closed_);
  list_add_tail(&pending_requests_, &request->provider_node);
}

void PhysicalPageProvider::ClearAsyncRequest(page_request_t* request) {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  if (list_in_list(&request->provider_node)) {
    list_delete(&request->provider_node);
  }

  // No need to chase down any currently-processing request here, since before processing a request,
  // we stash the values of all fields we need from the page_request_t under the lock.  So any
  // currently-processing request is independent from the page_request_t that started it.
}

void PhysicalPageProvider::SwapRequest(page_request_t* old, page_request_t* new_req) {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);

  if (list_in_list(&old->provider_node)) {
    list_replace_node(&old->provider_node, &new_req->provider_node);
  }
}

void PhysicalPageProvider::OnDetach() {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);
  detached_ = true;
}

void PhysicalPageProvider::OnClose() {
  Guard<Mutex> guard{&mtx_};
  ASSERT(!closed_);
  closed_ = true;
}

void PhysicalPageProvider::OnDispatcherClose() {
  // Not used for PhysicalPageProvider.
  DEBUG_ASSERT(false);
}

bool DecommitSupported() {
  return true;
}

zx_status_t PhysicalPageProvider::WaitOnEvent(Event* event) {
  while (true) {
    uint64_t request_offset;
    uint64_t request_length;
    {  // scope guard
      Guard<Mutex> guard{&mtx_};
      // closed_ can be true here, but if closed_ is true, then pending_requests_ is also empty, so
      // we won't process any more requests once closed_ is true.
      DEBUG_ASSERT(!closed_ || list_is_empty(&pending_requests_));
      if (list_is_empty(&pending_requests_)) {
        // Done with all requests (or remaining requests cancelled).
        goto requests_done;
      }
      page_request_t* request = list_remove_head_type(&pending_requests_, page_request_t, provider_node);
      DEBUG_ASSERT(request);
      request_offset = request->offset;
      request_length = request->length;
    }  // ~guard
    DEBUG_ASSERT(request_offset + request_length > request_offset);

    // If the user didn't use ZX_VMO_OP_CANCEL_LOAN, then this is the first place that's cancelling
    // the loan.
    pmm_cancel_loan(phys_base_ + request_offset, request_length / PAGE_SIZE);

    // Replace needed pages in other VMOs, so that needed pages become free.  This is iterating over
    // the destination offset in cow_pages_.  The needed pages can be scattered around in various
    // VMOs and offsets of those VMOs, and can be free (but loan_cancelled so they won't be picked
    // up for a new use), and may be becoming free as we're finding the pages in this loop.  Given
    // concurrent commit by other threads, we may also find some pages that are no longer loaned
    // (are in cow_pages_).  Given concurrent decommit by other threads, the
    // VmCowPages::CommitRangeLocked loop will re-commit pages in the current range, but nothing
    // stops a decommit from occurring to pages processed earlier while pages processed later are
    // still being committed.
    uint64_t request_end = request_offset + request_length;
    for (uint64_t offset = request_offset; offset < request_end; offset += PAGE_SIZE) {
      vm_page_t* page = paddr_to_vm_page(phys_base_ + offset);
      DEBUG_ASSERT(page);
      // We sometimes have to try more than once to chase down the page.  While we could limit the
      // number of potential iterations here without giving up too soon due to normal moves of page
      // among borrowing VmCowPages, the complexity doesn't seem worth the benefit so far.  Let's
      // see if we really need such a mitigation first.  If so, it might be reasonable enough to
      // have _all_ page mover(s) do the loan_cancelled page replacement themselves as a move is
      // completing, so that the max count of moves without transiting FREE completed after
      // loan_cancelled is true could be limited to 1.  This code could then limit to 1 the
      // number of iterations that accomodate a missing page, missing cow, or destructing cow (up to
      // 1 iteration each), else return ZX_ERR_BAD_STATE.
      while (true) {
        // If the page is in a transient temporary short duration state that breifly prevents
        // reclaim of the page, wait a short duration until the page has exited that state.
        //
        // Despite the efforts of GetCowWithReplaceablePage, we may still find below that the VmCowPages
        // doesn't have the page any more, which is the reason for the directly enclosing loop.
        auto get_cow_result = pmm_page_queues()->GetCowWithReplaceablePage(page, cow_pages_, 0);
        if (!get_cow_result.is_ok()) {
          // The only GetCowWithReplaceablePage failures are event Wait(deadline) failures, in which case just
          // return failure here rather than fail any page requests which would just propagate a
          // wait failure status via another wait, and possibly mess up other threads doing a
          // concurrent overlapping commit (which may as well work).
          return get_cow_result.error_value();
        }
        // Even if GetCowWithReplaceablePage was successful, there may not be a backlink if page already
        // became FREE.
        auto maybe_vmo_backlink = get_cow_result.value();
        if (!maybe_vmo_backlink) {
          // The page is/was FREE or already in cow_pages_, else GetCowWithReplaceablePage would have
          // continued trying.
          //
          // "break" is brittle; downward gotos are fine; simulates labeled break/continue which
          // are totally fine, but not present in C++.
          goto next_page;
        }
        auto& vmo_backlink = maybe_vmo_backlink.value();
        // Else GetCowWithReplaceablePage would have kept trying.
        DEBUG_ASSERT(vmo_backlink.cow);
        auto& cow = vmo_backlink.cow;
        // If it were equal, GetCowWithReplaceablePage would have returned success but without a backlink.
        DEBUG_ASSERT(cow.get() != cow_pages_);
        // vmo_backlink.offset is offset in cow, not in cow_pages_.
        zx_status_t replace_result = cow->ReplacePage(page, vmo_backlink.offset, kCommitFlagsForceReplaceLoaned);
        if (replace_result == ZX_ERR_NOT_FOUND) {
          // Either became FREE or was moved.  Go around again to figure out which and continue
          // chasing it down.
          goto this_page_again;
        }
        if (replace_result != ZX_OK) {
          // No other errors are possible here.
          DEBUG_ASSERT(ZX_ERR_NO_MEMORY);
          // This call fails all current VmCowPages requests that overlap this page.
          zx_status_t fail_page_requests_result = cow_pages_->FailPageRequests(
              offset, PAGE_SIZE, replace_result);
          DEBUG_ASSERT(fail_page_requests_result == ZX_OK);
          continue;
        }
        // The page has been replaced with a different page that doesn't have loan_cancelled set.
        //
        // aka "break", but more clear in my opinion
        goto next_page;
        this_page_again:;
      }  // while chasing page
      next_page:;
    }  // for pages of request

    // Finish processing request.

    // These are ordered by cow_pages_ offsets (destination offsets), but gaps are allowed, and the
    // ordering isn't relied on.  Requests are allowed to include offset ranges that have already
    // been committed by other threads, which can result in an offset gap in pages_in_transit.
    list_node pages_in_transit;
    list_initialize(&pages_in_transit);
    // Now try to get the FREE pages from PMM.  Due to concurrent decommit/commit, we may not get
    // all the pages, despite having taken all the necessary steps to cause the requested pages to
    // become FREE.  Some of the pages may have been re-used for another purpose so are not
    // presently FREE, or are not presently loaned (some of the pages may already be in cow_pages_).
    pmm_end_loan(phys_base_ + request_offset, request_length / PAGE_SIZE, &pages_in_transit);
    // Despite not getting all the requested pages, the only reasons we didn't get all the pages are
    // from concurrent decommit/commit.  Because this thread did all the necessary steps to get all
    // the pages, we can treat our failure to actually get all the pages as if it were success,
    // because the ordering of concurrent decommit/commit and this thread is arbitrary, and even if
    // we do succeed for all pages, a decommit immediately following can remove pages.  So the end
    // result is indistinguishable to the user.
    //
    // We will claim to have supplied all the pages, even though we didn't actually, because if it
    // weren't for interference from concurrent decommit/commit, we would have supplied them all.
    // Further, the interfering decommit could have occurred slightly later and we wouldn't have
    // been able to detect the interference here in the first place.  If not all pages end up
    // present in cow_pages_ on return to the user from the present commit, due to concurrent
    // decommit, that's just normal commit semantics.
    //
    // Supply the pages we got to cow_pages_, but also tell it what range to claim is supplied now,
    // even if not all the range is covered by pages_in_transit due to concurrent decommit/commit.
    // Some pages not supplied are potentially loaned out again due to concurrent decommit.  Some
    // are potentially already in cow_pages_ due to concurrent commit.  Either way, we're allowed
    // to claim the current commit is successful because the only reason it's not necessarily making
    // all requested pages present is concurrent decommit/commit.  If it happens to be two commits
    // fighting to put pages in cow_pages_ first, then both succeeding makes sense, and is what we
    // want.  If it's an interfering decommit, then that decommit could have come slightly later
    // anyway, so pretend as if the commit worked and then the decommit happened after, since that
    // could potentially occur anyway, and is indistinguishable from the user's point of view.
    zx_status_t supply_result = cow_pages_->SupplyNonZeroedPhysicalPages(request_offset, request_length, &pages_in_transit);
    if (supply_result != ZX_OK) {
      // Since supplying pages didn't work, give up on this whole request and fail the whole range.
      // This also fails any current requests that overlap any part of this range.
      zx_status_t fail_page_requests_result = cow_pages_->FailPageRequests(
          request_offset, request_length, supply_result);
      DEBUG_ASSERT(fail_page_requests_result == ZX_OK);
      continue;
    }
  }  // while have requests to process
  requests_done:;

  // Now that all the requests that existed when WaitOnEvent was called have been processed, we can
  // wait on the event (and it won't have any reason to block this thread, because every page of
  // every request that existed before this wait was started has been succeeded or failed by this
  // point).
  ktl::optional<ThreadDispatcher::AutoBlocked> by;
  // Checking only for benefit of Zircon unit tests.
  if (ThreadDispatcher::GetCurrent()) {
    by.emplace(ThreadDispatcher::Blocked::PAGER);
  }
  kcounter_add(physical_reclaim_total_requests, 1);
  // Will immediately return, because we've already processed all the requests that were pending
  // above (with success or failure).
  zx_status_t wait_result = event->Wait(Deadline::infinite());
  if (wait_result == ZX_OK) {
    kcounter_add(physical_reclaim_succeeded_requests, 1);
  } else {
    kcounter_add(physical_reclaim_failed_requests, 1);
  }
  if (wait_result != ZX_OK) {
    return wait_result;
  }
  return ZX_OK;
}

bool PhysicalPageProvider::DecommitSupported() {
  return true;
}

void PhysicalPageProvider::Dump() {
  Guard<Mutex> guard{&mtx_};
  printf(
    "physical_page_provider %p cow_pages_ %p phys_base_ 0x%" PRIx64 " closed %d", this, cow_pages_, phys_base_, closed_);
  page_request_t* req;
  list_for_every_entry (&pending_requests_, req, page_request_t, provider_node) {
    printf("  pending req [0x%lx, 0x%lx)\n", req->offset, req->length);
  }
}
