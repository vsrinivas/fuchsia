// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PAGER_PROXY_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PAGER_PROXY_H_

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
class PhysicalPageProvider : public PageProvider {
 public:
  PhysicalPageProvider(VmCowPages* cow_pages, paddr_t phys_base);
  ~PhysicalPageProvider() override;

 private:

  // PageProvider methods.
  bool GetPageSync(uint64_t offset, VmoDebugInfo vmo_debug_info, vm_page_t** const page_out,
                   paddr_t* const pa_out) final;
  void GetPageAsync(page_request_t* request) final;
  void ClearAsyncRequest(page_request_t* request) final;
  void SwapRequest(page_request_t* old, page_request_t* new_req) final;
  void OnClose() final;
  void OnDetach() final;
  // Before actually waiting on the event, uses the calling thread (which isn't holding any locks)
  // to process all the requests in pending_requests_.
  zx_status_t WaitOnEvent(Event* event) final;

  // Not used for PhysicalPageProvider.
  void OnDispatcherClose() final;

  bool DecommitSupported() final;

  void Dump() final;

  VmCowPages *const cow_pages_;
  const paddr_t phys_base_;

  mutable DECLARE_MUTEX(PhysicalPageProvider) mtx_;

  // Queue of page_request_t's that have come in while packet_ is busy. The
  // head of this queue is sent to the port when packet_ is freed.
  list_node_t pending_requests_ TA_GUARDED(mtx_) = LIST_INITIAL_VALUE(pending_requests_);

  bool detached_ TA_GUARDED(mtx_) = false;
  bool closed_ TA_GUARDED(mtx_) = false;

  // Queues the page request, putting it in pending_requests_;
  void QueueRequestLocked(page_request_t* request) TA_REQ(mtx_);

  friend PagerDispatcher;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PAGER_PROXY_H_
