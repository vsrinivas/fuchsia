// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>

__BEGIN_CDECLS

// Handles an asynchronous trap access.
//
// The |status| is |ZX_OK| if the bell was received and |bell| contains the
// information from the packet, otherwise |bell| is null.
typedef void(async_guest_bell_trap_handler_t)(async_dispatcher_t* dispatcher,
                                              async_guest_bell_trap_t* trap,
                                              zx_status_t status,
                                              const zx_packet_guest_bell_t* bell);

// Holds context for a bell trap and its handler.
//
// After successfully posting setting the trap, the client is responsible for retaining
// the structure in memory (and unmodified) until the guest has been destroyed or the
// dispatcher shuts down.  There is no way to cancel a trap which has been set.
struct async_guest_bell_trap {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;

    // The handler to invoke to handle the trap access.
    async_guest_bell_trap_handler_t* handler;
};

// Sets a bell trap in the guest to be handled asynchronously via a handler.
//
// |guest| is the handle of the guest the trap will be set on.
// |addr| is the base address for the trap in the guest's physical address space.
// |length| is the size of the trap in the guest's physical address space.
//
// Returns |ZX_OK| if the trap was successfully set.
// Returns |ZX_ERR_ACCESS_DENIED| if the guest does not have |ZX_RIGHT_WRITE|.
// Returns |ZX_ERR_ALREADY_EXISTS| if a bell trap with the same |addr| exists.
// Returns |ZX_ERR_INVALID_ARGS| if |addr| or |length| are invalid.
// Returns |ZX_ERR_OUT_OF_RANGE| if |addr| or |length| are out of range of the
// address space.
// Returns |ZX_ERR_WRONG_TYPE| if |guest| is not a handle to a guest.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// This operation is thread-safe.
zx_status_t async_set_guest_bell_trap(async_dispatcher_t* dispatcher, async_guest_bell_trap_t* trap,
                                      zx_handle_t guest, zx_vaddr_t addr, size_t length);

__END_CDECLS
