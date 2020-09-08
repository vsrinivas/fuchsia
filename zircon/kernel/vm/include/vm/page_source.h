// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_SOURCE_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_SOURCE_H_

#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/event.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <vm/page.h>
#include <vm/page_request.h>
#include <vm/vm.h>

// A page source has two parts - the PageSource and the PagerSource. The
// PageSource is responsible for generic functionality, mostly around managing
// the lifecycle of page requests. The PagerSource is responsible for actually
// providing the pages.
//
// The synchronous fulfillment of requests is fairly straightforward, with
// direct calls from the vm object to the PageSource to the PagerSource.
//
// For asynchronous requests, the lifecycle is as follows:
//   1) A vm object requests a page with PageSource::GetPage
//   2) PageSource starts tracking the request's PageRequest and then
//      forwards the request to PagerSource::GetPageAsync.
//   3) The caller waits for the request with PageRequest::Wait
//   4) At some point, whatever is backing the callback provides pages
//      to the vm object (e.g. with VmObjectPaged::SupplyPages).
//   5) The vm object calls PageSource::OnPagesSupplied, which signals
//      any PageRequests that have been fulfilled.
//   6) The caller wakes up and queries the vm object again, by which
//      point the request page will be present.

class PageRequest;

struct VmoDebugInfo {
  uintptr_t vmo_ptr;
  uint64_t vmo_id;
};

// Object which provides pages to a vm_object.
class PageSource : public fbl::RefCounted<PageSource> {
 public:
  PageSource();
  virtual ~PageSource();

  // Sends a request to the backing source to provide the requested page.
  //
  // Returns ZX_OK if the request was synchronously fulfilled.
  // Returns ZX_ERR_SHOULD_WAIT if the request will be asynchronously
  // fulfilled. The caller should wait on |req|.
  // Returns ZX_ERR_NEXT if the PageRequest is in batch mode and the caller
  // can continue to add more pages to the request.
  // Returns ZX_ERR_NOT_FOUND if the request cannot be fulfilled.
  zx_status_t GetPage(uint64_t offset, PageRequest* req, VmoDebugInfo vmo_debug_info,
                      vm_page_t** const page_out, paddr_t* const pa_out);

  // Called to complete a batched PageRequest if the last call to GetPage
  // returned ZX_ERR_NEXT.
  //
  // Returns ZX_ERR_SHOULD_WAIT if the PageRequest will be fulfilled after
  // being waited upon.
  // Returns ZX_ERR_NOT_FOUND if the request will never be resolved.
  zx_status_t FinalizeRequest(PageRequest* node);

  // Updates the request tracking metadata to account for pages [offset, offset + len) having
  // been supplied to the owning vmo.
  void OnPagesSupplied(uint64_t offset, uint64_t len);

  // Fails outstanding page requests in the range [offset, offset + len). Events associated with the
  // failed page requests are signaled with the |error_status|, and any waiting threads are
  // unblocked.
  void OnPagesFailed(uint64_t offset, uint64_t len, zx_status_t error_status);

  // Returns true if |error_status| is a valid pager failure error code, which can be used with
  // |OnPagesFailed|.
  //
  // Not every error code is supported, since these errors can get returned via a zx_vmo_read() or a
  // zx_vmo_op_range(), if those calls resulted in a page fault. So the |error_status| should be a
  // supported return error code for those syscalls.
  static bool IsValidFailureCode(zx_status_t error_status);

  // Detaches the source. All future calls into the page source will fail. All
  // pending read transactions are aborted. Pending flush transactions will still
  // be serviced.
  void Detach();

  // Closes the source. All pending transactions will be aborted and all future
  // calls will fail.
  void Close();

  void Dump() const;

 protected:
  // Synchronously gets a page from the backing source.
  virtual bool GetPage(uint64_t offset, VmoDebugInfo vmo_debug_info, vm_page_t** const page_out,
                       paddr_t* const pa_out) = 0;
  // Informs the backing source of a page request. The callback has ownership
  // of |request| until the async request is cancelled.
  virtual void GetPageAsync(page_request_t* request) = 0;
  // Informs the backing source that a page request has been fulfilled. This
  // must be called for all requests that are raised.
  virtual void ClearAsyncRequest(page_request_t* request) = 0;
  // Swaps the backing memory for a request. Assumes that |old|
  // and |new_request| have the same type, offset, and length.
  virtual void SwapRequest(page_request_t* old, page_request_t* new_req) = 0;

  // OnDetach is called once no more calls to GetPage/GetPageAsync will be made. It
  // will be called before OnClose and will only be called once.
  virtual void OnDetach() = 0;
  // After OnClose is called, no more calls will be made except for ::WaitOnEvent.
  virtual void OnClose() = 0;

  virtual zx_status_t WaitOnEvent(Event* event) = 0;

 private:
  fbl::Canary<fbl::magic("VMPS")> canary_;

  mutable DECLARE_MUTEX(PageSource) page_source_mtx_;
  bool detached_ TA_GUARDED(page_source_mtx_) = false;
  bool closed_ TA_GUARDED(page_source_mtx_) = false;

  // Tree of pending_request structs which have been sent to the callback. The list
  // is keyed by the end offset of the requests (not the start offsets).
  fbl::WAVLTree<uint64_t, PageRequest*> outstanding_requests_ TA_GUARDED(page_source_mtx_);

#ifdef DEBUG_ASSERT_IMPLEMENTED
  // Tracks the request currently being processed (only used for verifying batching assertions).
  PageRequest* current_request_ TA_GUARDED(page_source_mtx_) = nullptr;
#endif  // DEBUG_ASSERT_IMPLEMENTED

  // Sends a read request to the backing source, or queues the request if the needed
  // region has already been requested from the source.
  void RaiseReadRequestLocked(PageRequest* request) TA_REQ(page_source_mtx_);

  // Wakes up the given PageRequest and all overlapping requests, with an optional |status|.
  void CompleteRequestLocked(PageRequest* head, zx_status_t status = ZX_OK)
      TA_REQ(page_source_mtx_);

  // Removes |request| from any internal tracking. Called by a PageRequest if
  // it needs to abort itself.
  void CancelRequest(PageRequest* request) TA_EXCL(page_source_mtx_);

  friend PageRequest;
};

// Object which is used to make delayed page requests to a PageSource
class PageRequest : public fbl::WAVLTreeContainable<PageRequest*>,
                    public fbl::DoublyLinkedListable<PageRequest*> {
 public:
  // If |allow_batching| is true, then a single request can be used to service
  // multiple consecutive pages.
  explicit PageRequest(bool allow_batching = false) : allow_batching_(allow_batching) {}
  ~PageRequest();

  // Returns ZX_OK on success, or a permitted error code if the pager explicitly failed this page
  // request. Returns ZX_ERR_INTERNAL_INTR_KILLED if the thread was killed.
  zx_status_t Wait();

  DISALLOW_COPY_ASSIGN_AND_MOVE(PageRequest);

 private:
  // PageRequests passed to GetPage may or may not be initialized. offset_ must be checked
  // and the object must be initialized if necessary.
  void Init(fbl::RefPtr<PageSource> src, uint64_t offset, VmoDebugInfo vmo_debug_info);

  const bool allow_batching_;

  // The page source this request is currently associated with.
  fbl::RefPtr<PageSource> src_;
  // Event signaled when the request is fulfilled.
  AutounsignalEvent event_;
  // PageRequests are active if offset_ is not UINT64_MAX. In an inactive request, the
  // only other valid field is src_.
  uint64_t offset_ = UINT64_MAX;
  // The total length of the request.
  uint64_t len_ = 0;
  // The vmobject this page request is for.
  VmoDebugInfo vmo_debug_info_ = {};

  // Keeps track of the size of the request that still needs to be fulfilled. This
  // can become incorrect if some pages get supplied, decommitted, and then
  // re-supplied. If that happens, then it will cause the page request to complete
  // prematurely. However, page source clients should be operating in a loop to handle
  // evictions, so this will simply result in some redundant read requests to the
  // page source. Given the rarity in which this situation should arise, it's not
  // worth the complexity of tracking it.
  uint64_t pending_size_ = 0;

  // List node for overlapping requests.
  fbl::DoublyLinkedList<PageRequest*> overlap_;

  // Request struct for the PageSourceCallback
  page_request_t read_request_;

  uint64_t GetEnd() const {
    // Assert on overflow, since it means vmobject made an out-of-bounds request.
    uint64_t unused;
    DEBUG_ASSERT(!add_overflow(offset_, len_, &unused));

    return offset_ + len_;
  }

  uint64_t GetKey() const { return GetEnd(); }

  friend PageSource;
  friend fbl::DefaultKeyedObjectTraits<uint64_t, PageRequest>;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_SOURCE_H_
