// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_REQUEST_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_REQUEST_H_

#include <zircon/listnode.h>
#include <zircon/types.h>

// Callback from the pmm invoked when pages become available. Note that this callback
// is not given any information about how many pages are available. The callback should
// try to fulfill the request starting from |offset| and should populate |actual_supplied|
// with the amount that was actually able to be allocated. If this value is not equal to
// |length|, then the callback will be invoked again with updated args when more pages
// become available.
typedef void (*pages_available_cb_t)(void* ctx, uint64_t offset, uint64_t length,
                                     uint64_t* actual_supplied);
// Callback from the pmm invoked when the pmm will no longer make any calls using |ctx|.
typedef void (*drop_ref_cb_t)(void* ctx);

// Struct used for making delayed page requests to a page provider.
//
// Currently, the two types of page providers are the pmm and PagerSources.
typedef struct page_request {
  // Offset and length of the request. These should be initialized before being
  // passed to the provider, and should not be accessed after being passed to the provider.
  //
  // The pmm does not care about the units (i.e. bytes vs pages), as long as these fields are
  // consistent with each other and the implementation of |pages_available_cb|. PagerSources
  // expect units of pages.
  uint64_t offset;
  uint64_t length;

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
} page_request_t;

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_REQUEST_H_
