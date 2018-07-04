// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <lib/async/trap.h>
#include <lib/zx/guest.h>

namespace async {

// Holds context for a bell trap and its handler.
//
// After successfully posting setting the trap, the client is responsible for retaining
// the structure in memory (and unmodified) until the guest has been destroyed or the
// dispatcher shuts down.  There is no way to cancel a trap which has been set.
//
// Concrete implementations: |async::GuestBellTrap|, |async::GuestBellTrapMethod|.
// Please do not create subclasses of GuestBellTrapBase outside of this library.
class GuestBellTrapBase {
protected:
    explicit GuestBellTrapBase(async_guest_bell_trap_handler_t* handler);
    ~GuestBellTrapBase();

    GuestBellTrapBase(const GuestBellTrapBase&) = delete;
    GuestBellTrapBase(GuestBellTrapBase&&) = delete;
    GuestBellTrapBase& operator=(const GuestBellTrapBase&) = delete;
    GuestBellTrapBase& operator=(GuestBellTrapBase&&) = delete;

public:
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
    zx_status_t SetTrap(async_dispatcher_t* dispatcher, const zx::guest& guest,
                        zx_vaddr_t addr, size_t length);

protected:
    template <typename T>
    static T* Dispatch(async_guest_bell_trap_t* trap) {
        static_assert(offsetof(GuestBellTrapBase, trap_) == 0, "");
        auto self = reinterpret_cast<GuestBellTrapBase*>(trap);
        return static_cast<T*>(self);
    }

private:
    async_guest_bell_trap_t trap_;
};

// A bell trap whose handler is bound to a |async::Task::Handler| function.
//
// Prefer using |async::GuestBellTrapMethod| instead for binding to a fixed class member
// function since it is more efficient to dispatch.
class GuestBellTrap final : public GuestBellTrapBase {
public:
    // Handles an asynchronous trap access.
    //
    // The |status| is |ZX_OK| if the bell was received and |bell| contains the
    // information from the packet, otherwise |bell| is null.
    using Handler = fbl::Function<void(async_dispatcher_t* dispatcher, async::GuestBellTrap* trap,
                                       zx_status_t status, const zx_packet_guest_bell_t* bell)>;

    explicit GuestBellTrap(Handler handler = nullptr);
    ~GuestBellTrap();

    void set_handler(Handler handler) { handler_ = fbl::move(handler); }
    bool has_handler() const { return !!handler_; }

private:
    static void CallHandler(async_dispatcher_t* dispatcher, async_guest_bell_trap_t* trap,
                            zx_status_t status, const zx_packet_guest_bell_t* bell);

    Handler handler_;
};

// A bell trap whose handler is bound to a fixed class member function.
//
// Usage:
//
// class Foo {
//     void Handle(async_dispatcher_t* dispatcher, async::GuestBellTrapBase* trap, zx_status_t status,
//                 const zx_packet_guest_bell_t* bell) { ... }
//     async::GuestBellTrapMethod<Foo, &Foo::Handle> trap_{this};
// };
template <class Class,
          void (Class::*method)(async_dispatcher_t* dispatcher, async::GuestBellTrapBase* trap,
                                zx_status_t status, const zx_packet_guest_bell_t* bell)>
class GuestBellTrapMethod final : public GuestBellTrapBase {
public:
    explicit GuestBellTrapMethod(Class* instance)
        : GuestBellTrapBase(&GuestBellTrapMethod::CallHandler),
          instance_(instance) {}

private:
    static void CallHandler(async_dispatcher_t* dispatcher, async_guest_bell_trap_t* trap,
                            zx_status_t status, const zx_packet_guest_bell_t* bell) {
        auto self = Dispatch<GuestBellTrapMethod>(trap);
        (self->instance_->*method)(dispatcher, self, status, bell);
    }

    Class* const instance_;
};

} // namespace async
