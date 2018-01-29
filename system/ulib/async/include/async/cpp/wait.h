// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/dispatcher.h>
#include <async/wait.h>
#include <fbl/function.h>
#include <fbl/macros.h>

namespace async {

// C++ wrapper for a pending wait operation.
//
// This class is thread-safe.
//
// Example usage:
//
//   class Foo {
//       Foo() { wait_.set_handler(fbl::BindMember(this, &Foo::Handle)); }
//       async_wait_result_t Handle(...) { ... };
//       async::Wait wait_;
//   };
//
// Note that when set_handler() is used with fbl::BindMember() (as in the
// example above), async::WaitMethod should be used instead, if possible,
// because it is more efficient.  Using async::WaitMethod will generate
// less code and use fewer indirect jumps at run time for dispatching each
// event.
class Wait final : private async_wait_t {
public:
    // Handles completion of asynchronous wait operations.
    //
    // Reports the |status| of the wait.  If the status is |ZX_OK| then |signal|
    // describes the signal which was received, otherwise |signal| is null.
    //
    // The result indicates whether the wait should be repeated; it may
    // modify the wait's properties (such as the trigger) before returning.
    //
    // The result must be |ASYNC_WAIT_FINISHED| if |status| was not |ZX_OK|.
    //
    // It is safe for the handler to destroy itself when returning |ASYNC_WAIT_FINISHED|.
    using Handler = fbl::Function<async_wait_result_t(async_t* async,
                                                      zx_status_t status,
                                                      const zx_packet_signal_t* signal)>;

    // Initializes the properties of the wait operation.
    explicit Wait(zx_handle_t object = ZX_HANDLE_INVALID,
                  zx_signals_t trigger = ZX_SIGNAL_NONE, uint32_t flags = 0u);

    // Destroys the wait operation.
    //
    // This object must not be destroyed until the wait has completed, been
    // successfully canceled, or the asynchronous dispatcher itself has
    // been destroyed.
    ~Wait();

    // Gets or sets the handler to invoke when the wait completes.
    // Must be set before beginning the wait.
    const Handler& handler() const { return handler_; }
    void set_handler(Handler handler) { handler_ = fbl::move(handler); }

    // The object to wait for signals on.
    zx_handle_t object() const { return async_wait_t::object; }
    void set_object(zx_handle_t object) { async_wait_t::object = object; }

    // The set of signals to wait for.
    zx_signals_t trigger() const { return async_wait_t::trigger; }
    void set_trigger(zx_signals_t trigger) { async_wait_t::trigger = trigger; }

    // Valid flags: |ASYNC_FLAG_HANDLE_SHUTDOWN|.
    uint32_t flags() const { return async_wait_t::flags; }
    void set_flags(uint32_t flags) { async_wait_t::flags = flags; }

    // Begins asynchronously waiting for the object to receive one or more of
    // the trigger signals.
    //
    // See |async_begin_wait()| for details.
    zx_status_t Begin(async_t* async);

    // Cancels the wait.
    //
    // See |async_cancel_wait()| for details.
    zx_status_t Cancel(async_t* async);

private:
    static async_wait_result_t CallHandler(async_t* async, async_wait_t* wait,
                                           zx_status_t status, const zx_packet_signal_t* signal);

    Handler handler_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Wait);
};

// C++ wrapper for a pending wait operation, for binding to a fixed class
// member function.
//
// This class is thread-safe.
//
// Example usage:
//
//   class Foo {
//       async_wait_result_t Handle(...) { ... };
//       async::WaitMethod<Foo, &Foo::Handle> wait_{this};
//   };
//
// async::WaitMethod should be used in preference to async::Wait when
// possible, because it is more efficient when binding to class member
// functions.
template <class Class,
          async_wait_result_t (Class::*method)(
              async_t* async,
              zx_status_t status,
              const zx_packet_signal_t* signal)>
class WaitMethod : private async_wait_t {
public:
    explicit WaitMethod(Class* ptr,
                        zx_handle_t object = ZX_HANDLE_INVALID,
                        zx_signals_t trigger = ZX_SIGNAL_NONE,
                        uint32_t flags = 0u)
        : async_wait_t{{ASYNC_STATE_INIT}, &WaitMethod::CallHandler, object,
                       trigger, flags, {}},
          ptr_(ptr) {}

    // The object to wait for signals on.
    zx_handle_t object() const { return async_wait_t::object; }
    void set_object(zx_handle_t object) { async_wait_t::object = object; }

    // The set of signals to wait for.
    zx_signals_t trigger() const { return async_wait_t::trigger; }
    void set_trigger(zx_signals_t trigger) { async_wait_t::trigger = trigger; }

    // Valid flags: |ASYNC_FLAG_HANDLE_SHUTDOWN|.
    uint32_t flags() const { return async_wait_t::flags; }
    void set_flags(uint32_t flags) { async_wait_t::flags = flags; }

    // Begins asynchronously waiting for the object to receive one or more of
    // the trigger signals.
    //
    // See |async_begin_wait()| for details.
    zx_status_t Begin(async_t* async) {
        return async_begin_wait(async, this);
    }

    // Cancels the wait.
    //
    // See |async_cancel_wait()| for details.
    zx_status_t Cancel(async_t* async) {
        return async_cancel_wait(async, this);
    }

private:
    static async_wait_result_t CallHandler(async_t* async,
                                           async_wait_t* wait,
                                           zx_status_t status,
                                           const zx_packet_signal_t* signal) {
        return (static_cast<WaitMethod*>(wait)->ptr_->*method)(
            async, status, signal);
    }

    Class* const ptr_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(WaitMethod);
};

} // namespace async
