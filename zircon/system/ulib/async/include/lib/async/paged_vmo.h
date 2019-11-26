// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_PAGED_VMO_H_
#define LIB_ASYNC_PAGED_VMO_H_

#include <lib/async/dispatcher.h>

__BEGIN_CDECLS

// Handles port packets containing page requests.
//
// The |status| is |ZX_OK| if the packet was successfully delivered and |request|
// contains the information from the packet, otherwise |request| is null.
// The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down.
typedef void(async_paged_vmo_handler_t)(async_dispatcher_t* dispatcher,
                                        async_paged_vmo_t* paged_vmo, zx_status_t status,
                                        const zx_packet_page_request_t* request);

// Holds content for a paged request packet receiver and its handler.
//
// The client is responsible for retaining the structure in memory
// (and unmodified) until all packets have been received by the handler or the
// dispatcher shuts down.
struct async_paged_vmo {
  // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
  async_state_t state;

  // The handler to invoke when a packet is received.
  async_paged_vmo_handler_t* handler;

  // The associated pager when creating the VMO.
  zx_handle_t pager;

  // The VMO for this request.
  zx_handle_t vmo;
};

// Create a pager owned VMO.
//
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
// Other error values are possible. See the documentation for
// |zx_pager_create_vmo()|.
zx_status_t async_create_paged_vmo(async_dispatcher_t* dispatcher, async_paged_vmo_t* paged_vmo,
                                   uint32_t options, zx_handle_t pager, uint64_t vmo_size,
                                   zx_handle_t* vmo_out);

// Detach ownership of VMO from pager.
//
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
// Returns |ZX_ERR_BAD_HANDLE| if pager or vmo is not a valid handle.
// Returns |ZX_ERR_WRONG_TYPE| if pager is not a pager handle or vmo is not a vmo handle.
// Returns |ZX_ERR_INVALID_ARGS| if vmo is not a vmo created from pager.
// Other error values are possible. See the documentation for
// |zx_detach_paged_vmo()|.
zx_status_t async_detach_paged_vmo(async_dispatcher_t* dispatcher, async_paged_vmo_t* paged_vmo);

__END_CDECLS

#endif  // LIB_ASYNC_PAGED_VMO_H_
