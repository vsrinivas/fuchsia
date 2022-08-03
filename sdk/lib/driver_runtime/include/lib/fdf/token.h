// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TOKEN_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TOKEN_H_

#include <lib/fdf/dispatcher.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct fdf_token fdf_token_t;

// Handles the transfer of the runtime handle which was exchanged for the registered token.
// If |status| is ZX_OK, transfers ownership of |handle|.
//
// The status is |ZX_OK| if the token was successfully exchanged.
// The status is |ZX_ERR_CANCELED| if the dispatcher was shut down before the
// exchange was completed, or the peer token handle has been closed.
typedef void(fdf_token_exchange_handler_t)(fdf_dispatcher_t* dispatcher, fdf_token_t* token,
                                           fdf_status_t status, fdf_handle_t handle);

// Holds context for a registered token which is pending exchange for a runtime handle.
//
// After successfully registering the protocol, the client is responsible for retaining
// the structure in memory (and unmodified) until the exchange handler runs.
// Thereafter, this structure may be registered again or destroyed.
struct fdf_token {
  // The handler which will be called when a exchange is completed.
  fdf_token_exchange_handler_t* handler;
};

// Registers a token exchange handler for |token|.
//
// The exchange handler will be scheduled to be called on the dispatcher when a client calls
// |fdf_token_exchange| with the channel peer of |token|, If the connection has already been
// requested, the handler will be scheduled immediately.
//
// Transfers ownership of |token| to the runtime.
//
// Returns |ZX_OK| is the protocol was successfully registered.
// Returns |ZX_ERR_BAD_HANDLE| if |token| is not a valid channel handle.
// Returns |ZX_ERR_INVALID_ARGS| if |handler| or |dispatcher| is NULL.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down, or |handler|
// has already been registered.
fdf_status_t fdf_token_register(zx_handle_t token, fdf_dispatcher_t* dispatcher,
                                fdf_token_t* handler);

// Exchanges |token| with a fdf handle. The token exchange handler which was,
// or will be registered with the channel peer of |token| will receive |handle|.
//
// Transfers ownership of |token| to the runtime, and ownership of |handle| to
// the driver who registered the token.
//
// Returns |ZX_OK| if the |handle| has been transferred.
// Returns |ZX_ERR_BAD_HANDLE| if |token| is not a valid channel handle,
// or |handle| is not a valid FDF handle.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
fdf_status_t fdf_token_exchange(zx_handle_t token, fdf_handle_t handle);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TOKEN_H_
