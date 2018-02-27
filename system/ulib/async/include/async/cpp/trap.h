// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/dispatcher.h>
#include <async/trap.h>
#include <fbl/function.h>
#include <fbl/macros.h>

namespace async {

// C++ wrapper for an async trap handler.
template <class Class,
          void (Class::*method)(async_t* async, const zx_packet_guest_bell_t* bell)>
class GuestBellTrapMethod : private async_guest_bell_trap_t {
public:
    explicit GuestBellTrapMethod(Class* ptr,
                                 zx_handle_t guest = ZX_HANDLE_INVALID,
                                 zx_vaddr_t addr = 0,
                                 size_t length = 0)
        : async_guest_bell_trap_t{{ASYNC_STATE_INIT}, &GuestBellTrapMethod::CallHandler,
                                  guest, addr, length},
          ptr_(ptr) {}

    // The guest to trap on.
    zx_handle_t guest() const { return async_guest_bell_trap_t::guest; }
    void set_guest(zx_handle_t guest) { async_guest_bell_trap_t::guest = guest; }

    // The base address for the trap in guest physical address space.
    zx_vaddr_t addr() const { return async_guest_bell_trap_t::addr; }
    void set_addr(zx_vaddr_t addr) { async_guest_bell_trap_t::addr = addr; }

    // The size of the trap in guest physical address space..
    size_t length() const { return async_guest_bell_trap_t::length; }
    void set_length(size_t length) { async_guest_bell_trap_t::length = length; }

    zx_status_t Begin(async_t* async) {
        return async_set_guest_bell_trap(async, this);
    }

private:
    static void CallHandler(async_t* async, async_guest_bell_trap_t* trap,
                            const zx_packet_guest_bell_t* bell) {
        return (static_cast<GuestBellTrapMethod*>(trap)->ptr_->*method)(async, bell);
    }

    Class* const ptr_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(GuestBellTrapMethod);
};

} // namespace async
