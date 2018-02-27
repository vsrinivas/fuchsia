// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/dispatcher.h>

__BEGIN_CDECLS

// Handles an asyncronous trap access.
typedef void (*async_guest_bell_trap_handler_t)(async_t* async,
                                                async_guest_bell_trap_t* trap,
                                                const zx_packet_guest_bell_t* packet);

struct async_guest_bell_trap {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;
    // The handler to invoke to handle the trap access.
    async_guest_bell_trap_handler_t handler;
    // The guest this trap will be set on.
    zx_handle_t guest;
    // The base address for this trap in guest physical address space.
    zx_vaddr_t addr;
    // The size of this trap in guest physical address space.
    size_t length;
};

// Sets a bell trap in the guest to be handled asyncronously via a handler.
//
// Note that since it's currently not possible to remove a trap, it's up to the
// caller to ensure that |trap| outlives the |async| that it has been registered
// on.
inline zx_status_t async_set_guest_bell_trap(async_t* async, async_guest_bell_trap_t* trap) {
    return async->ops->set_guest_bell_trap(async, trap);
}

__END_CDECLS
