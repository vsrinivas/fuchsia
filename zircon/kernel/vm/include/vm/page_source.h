// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_SOURCE_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_SOURCE_H_

#include <zircon/types.h>

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/event.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <ktl/optional.h>
#include <ktl/unique_ptr.h>
#include <vm/page.h>
#include <vm/vm.h>

// At the high level the goal of the objects here is to
// 1. Trigger external entities to do work based on VMO operations, such as asking a pager to supply
//    a missing page of data.
// 2. Have a way for external entities to let the VMO system know these requests have been
//    fulfilled.
// 3. Provide a way for the high level caller, who may not know what actions are being performed on
//    what entities, to wait until their operation can be completed.
//
// The different objects can be summarized as:
//  * PageRequest: Caller allocated object that the caller uses to perform the Wait.
//  * PageRequestInterface: A reference to an object implementing this interface is held by the
//    PageRequest and provides a way for the PageRequest to interact with the underlying PageSource.
//  * PageSource: Performs request and overlap tracking, forwarding unique ranges of requests to the
//    underlying PageProvider.
//  * PageProvider: Asynchronously performs requests. Requests are completed by actions being
//    performed on the VMO.
//
// A typical flow would be
//  * User allocates PageRequest on the stack, and passes it in to some VMO operation
//  * VMO code needs something to happen and calls a PageSource method, passing in PageRequest it
//    had been given.
//  * PageSource populates fields of the PageRequest and adds it to the list of requests it is
//    tracking, and determines how this request overlaps with any others. Based on overlap, it may
//    or may not notify the underlying PageProvider that some work needs to be done (the page
//    provider will complete this asynchronously somehow).
//  * VMO returns ZX_ERR_SHOULD_WAIT and then the top level calls PageRequest::Wait
//  * PageRequest::Wait uses the PageRequestInterface to ask the underlying PageSource how to Wait
//    for the operation to complete
//  # As an optional path, if the PageRequest was not Waited on for some reason, the PageRequest
//    will also use the PageRequestInterface to inform the PageSource that this request is no longer
//    needed and can be canceled.
// For the other side, while the Wait is happening some other thread will
//  * Call a VMO operation, such as VmObject::SupplyPages
//  * VMO will perform the operation, and then let the PageSource know by the corresponding
//    interface method, such as OnPagesSupplied.
//  * PageSource will update request tracking, and notify any PageRequests that were waiting and can
//    be woken up.
//
// There is more complexity of implementation and API, largely to handle the fact that the
// PageRequest serves as the allocation of all data needed for all parties. Therefore every layer
// needs to be told when requests are coming and going to ensure they update any lists and do not
// refer to out of scope stack variables.

class PageRequest;
class PageSource;
class AnonymousPageRequester;

struct VmoDebugInfo {
  uintptr_t vmo_ptr;
  uint64_t vmo_id;
};

// The different types of page requests that can exist.
enum page_request_type : uint32_t {
  READ = 0,   // Request to provide the initial contents for the page.
  DIRTY,      // Request to alter contents of the page, i.e. transition it from clean to dirty.
  WRITEBACK,  // Request to write back modified page contents back to the source.
  COUNT       // Number of page request types.
};

inline const char* PageRequestTypeToString(page_request_type type) {
  switch (type) {
    case page_request_type::READ:
      return "READ";
    case page_request_type::DIRTY:
      return "DIRTY";
    case page_request_type::WRITEBACK:
      return "WRITEBACK";
    default:
      return "UNKNOWN";
  }
}

// These properties are constant per PageProvider type, so a given VmCowPages can query and cache
// these properties once (if it has a PageSource) and know they won't change after that.  This also
// avoids per-property plumbing via PageSource.
//
// TODO(dustingreen): (or rashaeqbal) Migrate more const per-PageProvider-type properties to
// PageSourceProperties, after the initial round of merging is done.
struct PageSourceProperties {
  // We use PageSource for both user pager and contiguous page reclaim.  This is how we tell whether
  // the PageSource is really a user pager when reporting to user mode that a given VMO is/isn't
  // user pager backed.  This property should not be used for other purposes since we can use more
  // specific properties for any behavior differences.
  const bool is_user_pager;

  // Currently, this is always equal to is_user_pager, but per the comment on is_user_pager, we
  // prefer to use more specific behavior properties rather than lean on is_user_pager.
  //
  // True iff providing page content.  This can be immutable page content, or it can be page content
  // that was potentially modified and written back previously.
  //
  // If this is false, the provider will ensure (possibly with VmCowPages help) that pages are
  // zeroed by the time they are added to the VmCowPages.
  const bool is_preserving_page_content;

  // Iff true, the PageSource (and PageProvider) must be used to allocate all pages.  Pre-allocating
  // generic pages from the pmm won't work.
  const bool is_providing_specific_physical_pages;

  // true - PageSource::FreePages() must be used instead of pmm_free().
  // false - pmm_free() must be used; PageSource::FreePages() will assert.
  const bool is_handling_free;
};

// Interface for providing pages to a VMO through page requests.
class PageProvider : public fbl::RefCounted<PageProvider> {
 public:
  virtual ~PageProvider() = default;

 protected:
  // Methods a PageProvider implementation can use to retrieve fields from a PageRequest.
  static page_request_type GetRequestType(const PageRequest* request);
  static uint64_t GetRequestOffset(const PageRequest* request);
  static uint64_t GetRequestLen(const PageRequest* request);

 private:
  // The returned properties will last at least as long as PageProvider.
  virtual const PageSourceProperties& properties() const = 0;

  // Informs the backing source of a page request. The provider has ownership
  // of |request| until the async request is cancelled.
  virtual void SendAsyncRequest(PageRequest* request) = 0;
  // Informs the backing source that a page request has been fulfilled. This
  // must be called for all requests that are raised.
  virtual void ClearAsyncRequest(PageRequest* request) = 0;
  // Swaps the backing memory for a request. Assumes that |old|
  // and |new_request| have the same type, offset, and length.
  virtual void SwapAsyncRequest(PageRequest* old, PageRequest* new_req) = 0;
  // This will assert unless is_handling_free is true, in which case this will make the pages FREE.
  virtual void FreePages(list_node* pages) {
    // If is_handling_free true, must implement FreePages().
    ASSERT(false);
  }
  // For asserting purposes only.  This gives the PageProvider a chance to check that a page is
  // consistent with any rules the PageProvider has re. which pages can go where in the VmCowPages.
  // PhysicalPageProvider implements this to verify that page at offset makes sense with respect to
  // phys_base_, since VmCowPages can't do that on its own due to lack of knowledge of phys_base_
  // and lack of awareness of contiguous.
  virtual bool DebugIsPageOk(vm_page_t* page, uint64_t offset) = 0;

  // OnDetach is called once no more calls to SendAsyncRequest will be made. It will be called
  // before OnClose and will only be called once.
  virtual void OnDetach() = 0;
  // After OnClose is called, no more calls will be made except for ::WaitOnEvent.
  virtual void OnClose() = 0;

  // Waits on an |event| associated with a page request.
  virtual zx_status_t WaitOnEvent(Event* event) = 0;

  // Dumps relevant state for debugging purposes.
  virtual void Dump(uint depth) = 0;

  // Whether the provider supports the |type| of page request. Controls which requests can be safely
  // forwarded to the provider.
  virtual bool SupportsPageRequestType(page_request_type type) const = 0;

  friend PageSource;
};

// Interface used by the page requests to communicate with the PageSource. Due to the nature of
// intrusive containers the RefCounted needs to be here and not on the PageSource to allow the
// PageRequest to hold a RefPtr just to this interface.
class PageRequestInterface : public fbl::RefCounted<PageRequestInterface> {
 public:
  virtual ~PageRequestInterface() = default;

 protected:
  PageRequestInterface() = default;

 private:
  friend PageRequest;
  // Instruct the page source that this request has been cancelled.
  virtual void CancelRequest(PageRequest* request) = 0;
  // Ask the page source to wait on this request, typically by forwarding to the page provider.
  // Note this gets called without a lock and so due to races the implementation needs to be
  // tolerant of having already been detached/closed.
  virtual zx_status_t WaitOnRequest(PageRequest* request) = 0;

  // Called to complete a batched PageRequest if the last call to GetPage returned
  // ZX_ERR_SHOULD_WAIT *and* the |request->BatchAccepting| is true.
  //
  // Returns ZX_ERR_SHOULD_WAIT if the PageRequest will be fulfilled after
  // being waited upon.
  // Returns ZX_ERR_NOT_FOUND if the request will never be resolved.
  virtual zx_status_t FinalizeRequest(PageRequest* request) = 0;
};

// A page source is responsible for fulfilling page requests from a VMO with backing pages.
// The PageSource class mostly contains generic functionality around managing
// the lifecycle of VMO page requests. The PageSource contains an reference to a PageProvider
// implementation, which is responsible for actually providing the pages. (E.g. for VMOs backed by a
// userspace pager, the PageProvider is a PagerProxy instance which talks to the userspace pager
// service.)
//
// The synchronous fulfillment of requests is fairly straightforward, with direct calls
// from the vm object to the PageSource to the PageProvider.
//
// For asynchronous requests, the lifecycle is as follows:
//   1) A vm object requests a page with PageSource::GetPage.
//   2) PageSource starts tracking the request's PageRequest and then
//      forwards the request to PageProvider::SendAsyncRequest.
//   3) The caller waits for the request with PageRequest::Wait.
//   4) At some point, whatever is backing the PageProvider provides pages
//      to the vm object (e.g. with VmObjectPaged::SupplyPages).
//   5) The vm object calls PageSource::OnPagesSupplied, which signals
//      any PageRequests that have been fulfilled.
//   6) The caller wakes up and queries the vm object again, by which
//      point the requested page will be present.
//
// For a contiguous VMO requesting physical pages back, step 4 above just frees the pages from some
// other use, and step 6 finds the requested pages available, but not yet present in the VMO,
// similar to what can happen with a normal PageProvider where pages can be read and then
// decommitted before the caller queries the vm object again.

// Object which provides pages to a vm_object.
class PageSource final : public PageRequestInterface {
 public:
  PageSource() = delete;
  explicit PageSource(fbl::RefPtr<PageProvider>&& page_provider);

  // Sends a request to the backing source to provide the requested page.
  //
  // Returns ZX_OK if the request was synchronously fulfilled.
  // Returns ZX_ERR_NOT_FOUND if the request cannot be fulfilled.
  // Returns ZX_ERR_SHOULD_WAIT if the request will be asynchronously fulfilled. If
  // |req->BatchAccepting| is true then additional calls to |GetPage| may be performed to add more
  // pages to the request, or if no more pages want to be added the request should be finalized by
  // |req->FinalizeRequest|. If |BatchAccepting| was false, or |req| was finalized, then the caller
  // should wait on |req|.
  zx_status_t GetPage(uint64_t offset, PageRequest* req, VmoDebugInfo vmo_debug_info,
                      vm_page_t** const page_out, paddr_t* const pa_out);

  void FreePages(list_node* pages);

  // For asserting purposes only.  This gives the PageProvider a chance to check that a page is
  // consistent with any rules the PageProvider has re. which pages can go where in the VmCowPages.
  // PhysicalPageProvider implements this to verify that page at offset makes sense with respect to
  // phys_base_, since VmCowPages can't do that on its own due to lack of knowledge of phys_base_
  // and lack of awareness of contiguous.
  bool DebugIsPageOk(vm_page_t* page, uint64_t offset);

  // Updates the request tracking metadata to account for pages [offset, offset + len) having
  // been supplied to the owning vmo.
  void OnPagesSupplied(uint64_t offset, uint64_t len);

  // Fails outstanding page requests in the range [offset, offset + len). Events associated with the
  // failed page requests are signaled with the |error_status|, and any waiting threads are
  // unblocked.
  void OnPagesFailed(uint64_t offset, uint64_t len, zx_status_t error_status);

  // Returns true if |error_status| is a valid ZX_PAGER_OP_FAIL failure error code (input, specified
  // by user mode pager).  These codes can be used with |OnPagesFailed| (and so can any failure
  // codes for which IsValidInternalFailureCode() returns true).
  //
  // Not every error code is supported, since these errors can get returned via a zx_vmo_read() or a
  // zx_vmo_op_range(), if those calls resulted in a page fault.  So the |error_status| should be a
  // supported return error code for those syscalls _and_ be an error code that we want to be
  // supported for the user mode pager to specify via ZX_PAGER_OP_FAIL.  Currently,
  // IsValidExternalFailureCode(ZX_ERR_NO_MEMORY) returns false, as we don't want ZX_ERR_NO_MEMORY
  // to be specified via ZX_PAGER_OP_FAIL (at least so far).
  static bool IsValidExternalFailureCode(zx_status_t error_status);

  // Returns true if |error_status| is a valid provider failure error code, which can be used with
  // |OnPagesFailed|.
  //
  // This returns true for every error code that IsValidExternalFailureCode() returns true for, plus
  // any additional error codes that are valid as an internal PageProvider status but not valid for
  // ZX_PAGER_OP_FAIL.
  //
  // ZX_ERR_NO_MEMORY will return true, unlike IsValidExternalFailureCode(ZX_ERR_NO_MEMORY) which
  // returns false.
  //
  // Not every error code is supported, since these errors can get returned via a zx_vmo_read() or a
  // zx_vmo_op_range(), if those calls resulted in a page fault.  So the |error_status| should be a
  // supported return error code for those syscalls.  An error code need not be specifiable via
  // ZX_PAGER_OP_FAIL for this function to return true.
  static bool IsValidInternalFailureCode(zx_status_t error_status);

  // Whether transitions from clean to dirty should be trapped.
  bool ShouldTrapDirtyTransitions() const {
    return page_provider_->SupportsPageRequestType(page_request_type::DIRTY);
  }

  // Request the page provider for clean pages in the range [offset, offset + len) to become dirty,
  // in order for a write to proceed. Returns ZX_ERR_SHOULD_WAIT if the request will be
  // asynchronously fulfilled; the caller should wait on |request|. Depending on the state of pages
  // in the range, the |request| might be generated for a range that is a subset of
  // [offset, offset + len).
  zx_status_t RequestDirtyTransition(PageRequest* request, uint64_t offset, uint64_t len,
                                     VmoDebugInfo vmo_debug_info);

  // Updates the request tracking metadata to account for pages [offset, offset + len) having
  // been dirtied in the owning VMO.
  void OnPagesDirtied(uint64_t offset, uint64_t len);

  // Detaches the source from the VMO. All future calls into the page source will fail. All
  // pending read transactions are aborted. Pending flush transactions will still
  // be serviced.
  void Detach();

  // Closes the source. Will call Detach() if the source is not already detached. All pending
  // transactions will be aborted and all future calls will fail.
  void Close();

  // The returned properties will last at least until Detach() or Close().
  const PageSourceProperties& properties() const { return page_provider_properties_; }

  void Dump(uint depth) const;

 protected:
  // destructor should only be invoked from RefPtr
  virtual ~PageSource();
  friend fbl::RefPtr<PageSource>;

 private:
  fbl::Canary<fbl::magic("VMPS")> canary_;

  mutable DECLARE_MUTEX(PageSource) page_source_mtx_;
  bool detached_ TA_GUARDED(page_source_mtx_) = false;
  bool closed_ TA_GUARDED(page_source_mtx_) = false;
  // We cache the immutable page_provider_->properties() to avoid many virtual calls.
  const PageSourceProperties page_provider_properties_;

  // Trees of outstanding requests which have been sent to the PageProvider, one for each supported
  // page request type. These lists are keyed by the end offset of the requests (not the start
  // offsets).
  fbl::WAVLTree<uint64_t, PageRequest*> outstanding_requests_[page_request_type::COUNT] TA_GUARDED(
      page_source_mtx_);

#ifdef DEBUG_ASSERT_IMPLEMENTED
  // Tracks the request currently being processed (only used for verifying batching assertions).
  PageRequest* current_request_ TA_GUARDED(page_source_mtx_) = nullptr;
#endif  // DEBUG_ASSERT_IMPLEMENTED

  // PageProvider instance that will provide pages asynchronously (e.g. a userspace pager, see
  // PagerProxy for details).
  const fbl::RefPtr<PageProvider> page_provider_;

  // Helper that adds page at |offset| to |request| and potentially forwards it to the provider.
  // |request| must already be initialized, and its page_request_type must be set to |type|.
  // |offset| must be page-aligned.
  //
  // Returns ZX_ERR_NEXT if the PageRequest::batch_state_ is BatchState::Internal and more pages can
  // be added to it, in which case the caller of this function within PageSource *must* handle
  // ZX_ERR_NEXT itself before returning from PageSource. This option is used for request types that
  // the PageSource would like to operate in batch mode by default as an optimization (e.g. DIRTY
  // requests), where pages are added to the batch internally in PageSource without involving the
  // external caller. In other words, the ZX_ERR_NEXT must be consumed internally within the
  // PageSource caller, as the external caller is not prepared to handle it.
  //
  // Otherwise this method always returns ZX_ERR_SHOULD_WAIT, and transitions the
  // PageRequest::batch_state_ as required.
  zx_status_t PopulateRequestLocked(PageRequest* request, uint64_t offset, page_request_type type)
      TA_REQ(page_source_mtx_);

  // Helper used to complete a batched page request if the last call to PopulateRequestLocked
  // left the page request in the BatchRequest::Accepting state.
  zx_status_t FinalizeRequestLocked(PageRequest* request) TA_REQ(page_source_mtx_);

  // Sends a request to the backing source, or adds the request to the overlap_ list if
  // the needed region has already been requested from the source.
  void SendRequestToProviderLocked(PageRequest* request) TA_REQ(page_source_mtx_);

  // Wakes up the given PageRequest and all overlapping requests, with an optional |status|.
  void CompleteRequestLocked(PageRequest* request, zx_status_t status = ZX_OK)
      TA_REQ(page_source_mtx_);

  // Helper that updates request tracking metadata to resolve requests of |type| in the range
  // [offset, offset + len).
  void ResolveRequests(page_request_type type, uint64_t offset, uint64_t len)
      TA_EXCL(page_source_mtx_);

  // Removes |request| from any internal tracking. Called by a PageRequest if
  // it needs to abort itself.
  void CancelRequest(PageRequest* request) override TA_EXCL(page_source_mtx_);

  zx_status_t WaitOnRequest(PageRequest* request) override;

  zx_status_t FinalizeRequest(PageRequest* request) override;
};

// The PageRequest provides the ability to be in two difference linked list. One owned by the page
// source (for overlapping requests), and one owned by the page provider (for tracking outstanding
// requests). These tags provide a way to distinguish between the two containers.
struct PageSourceTag;
struct PageProviderTag;
// Object which is used to make delayed page requests to a PageSource
class PageRequest : public fbl::WAVLTreeContainable<PageRequest*>,
                    public fbl::ContainableBaseClasses<
                        fbl::TaggedDoublyLinkedListable<PageRequest*, PageSourceTag>,
                        fbl::TaggedDoublyLinkedListable<PageRequest*, PageProviderTag>> {
 public:
  // If |allow_batching| is true, then a single request can be used to service
  // multiple consecutive pages.
  explicit PageRequest(bool allow_batching = false)
      : creation_batch_state_(allow_batching ? BatchState::Accepting : BatchState::Unbatched),
        batch_state_(creation_batch_state_) {}
  ~PageRequest();

  // Returns ZX_OK on success, or a permitted error code if the backing page provider explicitly
  // failed this page request. Returns ZX_ERR_INTERNAL_INTR_KILLED if the thread was killed.
  zx_status_t Wait();

  // Forwards to the underlying PageRequestInterface::FinalizeRequest, see that for details.
  zx_status_t FinalizeRequest();

  // If initialized, asks the underlying PageRequestInterface to abort this request, by calling
  // PageRequestInterface::CancelRequest.
  void CancelRequest();

  // Returns |true| if this is a batch request that can still accept additional requests. If |true|
  // the |FinalizeRequest| method must be called before |Wait| can be used. If this is |false| then
  // either this is not a batch request, or the batch request has already been closed and does not
  // need to be finalized.
  bool BatchAccepting() const { return batch_state_ == BatchState::Accepting; }

  DISALLOW_COPY_ASSIGN_AND_MOVE(PageRequest);

 private:
  // The batch state is used both to implement a stateful query of whether a batch page request is
  // finished taking new requests or not, and to implement assertions to catch misuse of the request
  // API.
  enum class BatchState {
    // Does not support batching.
    Unbatched,
    // Supports batching and can keep taking new requests. A request in this state must have
    // FinalizeRequest called before it can be waited on.
    Accepting,
    // This was a batched request that has been finalized and may be waited on.
    Finalized,
    // The caller did not request batching, but the PageSource internally decided to batch the
    // request as an optimization. Internal is treated differently from Accepting even though they
    // both notionally refer to batches, as Internal is not finalized externally (outside of
    // PageSource) as is the case for Accepting. The Internal state is managed internally by the
    // PageSource, so it is never transitioned to Finalized when the batch is ready to be waited on.
    // Since the "batch" in the case of Internal is not exposed to the external caller, there is
    // also a difference in the way page request failures are handled by the PageSource.
    // Only used for page_request_type::DIRTY. See comment near PageSource::PopulateRequestLocked
    // for more context.
    // TODO(rashaeqbal): Figure out if Internal can be unified with Accepting by making all batches
    // external.
    Internal
  };

  // TODO: PageSource and AnonymousPageRequest should not have direct access, but should rather have
  // their access mediate by the PageRequestInterface class that they derive from.
  friend PageSource;
  friend AnonymousPageRequester;

  friend PageProvider;
  friend fbl::DefaultKeyedObjectTraits<uint64_t, PageRequest>;

  // PageRequests may or may not be initialized, to support batching of requests. offset_ must be
  // checked and the object must be initialized if necessary (an uninitialized request has offset_
  // set to UINT64_MAX).
  void Init(fbl::RefPtr<PageRequestInterface> src, uint64_t offset, page_request_type type,
            VmoDebugInfo vmo_debug_info, bool internal_batching = false);

  bool IsInitialized() const { return offset_ != UINT64_MAX; }

  uint64_t GetEnd() const {
    // Assert on overflow, since it means vmobject made an out-of-bounds request.
    uint64_t unused;
    DEBUG_ASSERT(!add_overflow(offset_, len_, &unused));

    return offset_ + len_;
  }

  uint64_t GetKey() const { return GetEnd(); }

  // The batch state the external caller created this request with. A single page request object can
  // be reused multiple times by calling Init in between uses, at which point the batch_state_ is
  // reset to this value (unless overridden by internal_batching).
  const BatchState creation_batch_state_;
  // The current batch state of this request. Used to determine what operations are legal to
  // perform on the request.
  BatchState batch_state_;

  // The page source this request is currently associated with.
  fbl::RefPtr<PageRequestInterface> src_;
  // Event signaled when the request is fulfilled.
  AutounsignalEvent event_;
  // PageRequests are active if offset_ is not UINT64_MAX. In an inactive request, the
  // only other valid field is src_. Whilst a request is with a PageProvider (i.e. SendAsyncRequest
  // has been called), these fields must be kept constant so the PageProvider can read them. Once
  // the request has been cleared either by SwapAsyncRequest or ClearAsyncRequest they can be
  // modified again. The provider_owned_ bool is used for assertions to validate this flow, but
  // otherwise has no functional affect.
  bool provider_owned_ = false;
  uint64_t offset_ = UINT64_MAX;
  // The total length of the request.
  uint64_t len_ = 0;
  // The type of the page request.
  page_request_type type_;
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

  // Linked list for overlapping requests.
  fbl::TaggedDoublyLinkedList<PageRequest*, PageSourceTag> overlap_;
};

// Declare page provider helpers inline now that PageRequest has been defined.
inline page_request_type PageProvider::GetRequestType(const PageRequest* request) {
  DEBUG_ASSERT(request->provider_owned_);
  return request->type_;
}
inline uint64_t PageProvider::GetRequestOffset(const PageRequest* request) {
  DEBUG_ASSERT(request->provider_owned_);
  return request->offset_;
}
inline uint64_t PageProvider::GetRequestLen(const PageRequest* request) {
  DEBUG_ASSERT(request->provider_owned_);
  return request->len_;
}

// Wrapper around PageRequest that performs construction on first access. This is useful when a
// PageRequest needs to be allocated eagerly in case it is used, even if the common case is that it
// will not be needed.
class LazyPageRequest {
 public:
  // If |allow_batching| is true, then a single request can be used to service
  // multiple consecutive pages.
  explicit LazyPageRequest(bool allow_batching = false) : allow_batching_(allow_batching) {}
  ~LazyPageRequest() = default;

  // Initialize and return the internal PageRequest.
  PageRequest* get();

  PageRequest* operator->() { return get(); }

  PageRequest& operator*() { return *get(); }

 private:
  const bool allow_batching_;
  ktl::optional<PageRequest> request_ = ktl::nullopt;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_SOURCE_H_
