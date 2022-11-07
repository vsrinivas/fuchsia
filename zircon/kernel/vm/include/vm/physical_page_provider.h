// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSICAL_PAGE_PROVIDER_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSICAL_PAGE_PROVIDER_H_

#include <sys/types.h>
#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <object/port_dispatcher.h>
#include <vm/page_source.h>

// Page provider implementation that provides requested loaned physical pages.
//
// This is used by contiguous VMOs which have had pages decommitted, when the pages are again
// committed.  The reason we use a PageProvider for this is it lines up well with the pager model in
// the sense that a page request can be processed while not holding the contiguous VMO's lock.
//
// PhysicalPageProvider always operates async (similar to PagerProxy), because we'd like to (in
// typical non-overlapping commit/decommit usage) have one batch that covers the entire commit,
// regardless of the fact that some of the pages may already be free and therefore could be
// immediately obtained.  Quite often at least one page will be presently owned by a different
// VMO, so we may as well always do one big async batch that deals with all the presently
// non-FREE pages.
class PhysicalPageProvider : public PageProvider {
 public:
  explicit PhysicalPageProvider(uint64_t size);
  ~PhysicalPageProvider() override;

  // Called shortly after construction, before any use.
  void Init(VmCowPages* cow_pages, PageSource* page_source, paddr_t phys_base);

 private:
  // PageProvider methods.
  const PageSourceProperties& properties() const final;
  void SendAsyncRequest(PageRequest* request) final;
  void ClearAsyncRequest(PageRequest* request) final;
  void SwapAsyncRequest(PageRequest* old, PageRequest* new_req) final;
  void FreePages(list_node* pages) final;
  bool DebugIsPageOk(vm_page_t* page, uint64_t offset) final;
  // This also calls pmm_delete_lender()
  void OnClose() final;
  void OnDetach() final;
  // Before actually waiting on the event, uses the calling thread (which isn't holding any locks)
  // to process all the requests in pending_requests_.
  zx_status_t WaitOnEvent(Event* event) final;

  void Dump(uint depth) final;

  bool SupportsPageRequestType(page_request_type type) const final;

  // If there are no more requests in pending_requests_, returns false and doesn't modify the out
  // parameters.
  //
  // If pending_requests_ has another request, returns true and sets *reqeust_offset to the offset
  // and *request_length to the length.
  bool DequeueRequest(uint64_t* request_offset, uint64_t* request_length);

  static constexpr paddr_t kInvalidPhysBase = static_cast<paddr_t>(-1);
  // Only written once, under mtx_, just after construction.  Reads after that don't need to be
  // under mtx_.
  //
  // This is set to the physical base address of the associated VmCowPages.  We track this here
  // and not in VmCowPages because VmCowPages intentionally doesn't directly know about contiguous
  // concerns such as phys_base_.
  paddr_t phys_base_ = kInvalidPhysBase;
  // Set to the total size in bytes of the associated VmCowPages, to allow for asserts that requests
  // are in range.  While we could ask VmCowPages each time, the size_ doesn't change for contiguous
  // VMOs, so caching here keeps things simpler (for now).
  const uint64_t size_ = 0;

  VmCowPages* cow_pages_ = nullptr;
  PageSource* page_source_ = nullptr;

  mutable DECLARE_MUTEX(PhysicalPageProvider) mtx_;

  // Queue of page_request_t's that have come in while packet_ is busy. The
  // head of this queue is sent to the port when packet_ is freed.
  fbl::TaggedDoublyLinkedList<PageRequest*, PageProviderTag> pending_requests_ TA_GUARDED(mtx_);

  bool detached_ TA_GUARDED(mtx_) = false;
  bool closed_ TA_GUARDED(mtx_) = false;

  // Queues the page request, putting it in pending_requests_;
  void QueueRequestLocked(PageRequest* request) TA_REQ(mtx_);
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSICAL_PAGE_PROVIDER_H_
