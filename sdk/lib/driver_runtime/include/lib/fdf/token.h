// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TOKEN_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TOKEN_H_

#include <lib/fdf/dispatcher.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Tokens provide a mechanism for transferring FDF handles between drivers in the same process
// when a driver FIDL transport is not available. This is necessary as FDF handles cannot be
// transferred using the Zircon channel FIDL transport.
//
// A token is represented as a Zircon channel pair.
//
// # Example
//
//   // Child driver
//
//   void my_function() {
//     zx_channel_t token_local, token_remote;
//     zx_status_t status = zx_channel_create(0, &token_local, &token_remote);
//     ...
//     // Transfer |token_remote| to parent driver, perhaps over FIDL.
//     ...
//     fdf_handle_t channel_local, channel_remote;
//     status = fdf_channel_create(0, &channel_local, &channel_remote);
//     ...
//     zx_status_t status = fdf_token_transfer(token_local, channel_remote);
//     // The FDF handle |channel_remote| can be received by the parent
//     // driver who has the other side of the token.
//   }
//
//   // Parent driver
//
//   void token_transfer_handler(fdf_dispatcher_t* dispatcher, fdf_token_t* token_handler,
//                               zx_status_t status,
//                               fdf_handle_t handle) {
//     // Do something with the received FDF handle.
//     ...
//   }
//
//   void my_function() {
//     zx_handle_t token;
//     // Token received from child driver.
//     ...
//     // Register a handler for the token.
//     fdf_token_t token_handler{token_transfer_handler};
//     zx_status_t status = fdf_token_register(token, driver_dispatcher(), &token_handler));
//     ...
//   }
typedef struct fdf_token fdf_token_t;

// Handles the transfer of the fdf handle.
//
// If |status| is ZX_OK, transfers ownership of |handle|.
//
// The status is |ZX_OK| if the token was successfully transferred.
// The status is |ZX_ERR_CANCELED| if the dispatcher was shut down before the
// transfer was completed, or the peer token handle has been closed.
typedef void(fdf_token_transfer_handler_t)(fdf_dispatcher_t* dispatcher, fdf_token_t* token,
                                           zx_status_t status, fdf_handle_t handle);

// Holds context for a registered token which is waiting for an fdf handle to be transferred.
//
// After successfully registering the protocol, the client is responsible for retaining
// the structure in memory (and unmodified) until the transfer handler runs.
// Thereafter, this structure may be registered again or destroyed.
struct fdf_token {
  // The handler which will be called when an fdf handle transfer is completed.
  fdf_token_transfer_handler_t* handler;
};

// Registers a token transfer handler for |token|.
//
// The transfer handler will be scheduled to be called on the dispatcher when a client calls
// |fdf_token_transfer| with the channel peer of |token|, If the connection has already been
// requested, the handler will be scheduled immediately.
//
// Transfers ownership of |token| to the runtime.
//
// All handles are consumed and are no longer available to the caller, on success or failure.
//
// # Errors
//
// ZX_ERR_BAD_HANDLE: |token| is not a valid channel handle.
//
// ZX_ERR_INVALID_ARGS: |handler| or |dispatcher| is NULL.
//
// ZX_ERR_BAD_STATE: The dispatcher is shutting down, or |handler|
// has already been registered.
zx_status_t fdf_token_register(zx_handle_t token, fdf_dispatcher_t* dispatcher,
                               fdf_token_t* handler);

// Transfers the fdf handle to the owner of the channel peer of |token|.
//
// The token transfer handler which was, or will be registered with the
// channel peer of |token| will receive |handle|.
//
// Transfers ownership of |token| to the runtime, and ownership of |handle| to
// the driver who registered the token.
//
// All handles are consumed and are no longer available to the caller, on success or failure.
//
// # Errors
//
// ZX_ERR_BAD_HANDLE: |token| is not a valid channel handle,
// or |handle| is not a valid FDF handle.
//
// ZX_ERR_BAD_STATE: The dispatcher is shutting down.
zx_status_t fdf_token_transfer(zx_handle_t token, fdf_handle_t handle);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TOKEN_H_
