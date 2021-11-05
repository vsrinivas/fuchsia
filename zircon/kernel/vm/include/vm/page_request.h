// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_REQUEST_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_REQUEST_H_

#include <zircon/listnode.h>
#include <zircon/types.h>

// The different types of page requests that can exist.
enum page_request_type : uint32_t {
  READ = 0,   // Request to provide the initial contents for the page.
  DIRTY,      // Request to alter contents of the page, i.e. transition it from clean to dirty.
  WRITEBACK,  // Request to write back modified page contents back to the source.
  COUNT       // Number of page request types.
};

// Callback from the pmm invoked when pages become available. Note that this callback
// is not given any information about how many pages are available. The callback should
// try to fulfill the request starting from |offset| and should populate |actual_supplied|
// with the amount that was actually able to be allocated. If this value is not equal to
// |length|, then the callback will be invoked again with updated args when more pages
// become available.
using pages_available_cb_t = void (*)(void *, uint64_t, uint64_t, uint64_t *);
// Callback from the pmm invoked when the pmm will no longer make any calls using |ctx|.
using drop_ref_cb_t = void (*)(void *);

// Struct used for making delayed page requests to a page provider.
//
// Currently, the two types of page providers are the pmm and PagerProxy.
struct page_request {
  // Offset and length of the request. These should be initialized before being
  // passed to the provider, and should not be accessed after being passed to the provider.
  //
  // The pmm does not care about the units (i.e. bytes vs pages), as long as these fields are
  // consistent with each other and the implementation of |pages_available_cb|. PagerProxy
  // expects units of pages.
  uint64_t offset;
  uint64_t length;
  // The type of the page request. This should be initialized before being passed to the provider.
  page_request_type type;

  // Members only used by the pmm provider. Callbacks are executed on a dedicated
  // thread with no locks held.
  pages_available_cb_t pages_available_cb;
  drop_ref_cb_t drop_ref_cb;
  // ctx to use when invoking the above callbacks. The pmm may temporarily retain a
  // reference to cb_ctx even after the request is completed or cancelled, so the caller
  // needs to ensure that cb_ctx remains valid until drop_ref_cb is invoked.
  void* cb_ctx;

  // List node used by the page provider.
  list_node_t provider_node;
};
using page_request_t = struct page_request;

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_REQUEST_H_
