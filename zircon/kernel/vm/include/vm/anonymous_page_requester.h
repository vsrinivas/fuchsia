// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_ANONYMOUS_PAGE_REQUESTER_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_ANONYMOUS_PAGE_REQUESTER_H_

#include <lib/lazy_init/lazy_init.h>

#include <fbl/ref_ptr.h>
#include <vm/page_source.h>

// Implements the PageRequestInterface for anonymous pages. Unlike a PageSource, where the
// PageRequest unblocks once the page is installed in the VMO, this creates a PageRequest that
// unblocks once the PMM would succeed the allocation.
// Since this is intended to be used as a consequence of PMM allocations failing, and not
// specifically page content being missing, just the PageRequestInterface is implemented, and not
// the full PageSource interface.
class AnonymousPageRequester : public PageRequestInterface {
 public:
  // Fills in the given request such that it can be waited on. This is similar to
  // PageSource::GetPage except that all the unnecessary parameters are removed since the page
  // request will wait generically on the PMM, and not for any particular page to be provided.
  // For batched requests this will always finalize the request, since no useful information is
  // gained by attempting to find additional pages.
  zx_status_t FillRequest(PageRequest* req);

  // Requests the singleton instance.
  static AnonymousPageRequester& Get();

 private:
  AnonymousPageRequester() = default;
  ~AnonymousPageRequester() override = default;

  static void Init();

  // PageRequestInterface methods
  void CancelRequest(PageRequest* request) override { request->offset_ = UINT64_MAX; }
  zx_status_t WaitOnRequest(PageRequest* request) override;
  zx_status_t FinalizeRequest(PageRequest* request) override { return ZX_ERR_SHOULD_WAIT; }

  friend void vm_init_preheap();
  friend lazy_init::Access;
  friend fbl::RefPtr<AnonymousPageRequester>;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_ANONYMOUS_PAGE_REQUESTER_H_
