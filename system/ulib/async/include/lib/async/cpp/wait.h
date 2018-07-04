// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <lib/async/wait.h>

namespace async {

// Holds context for an asynchronous wait and its handler, with RAII semantics.
// Automatically cancels the wait when it goes out of scope.
//
// After successfully beginning the wait, the client is responsible for retaining
// the structure in memory (and unmodified) until the wait's handler runs, the wait
// is successfully canceled, or the dispatcher shuts down.  Thereafter, the wait
// may be started begun or destroyed.
//
// This class must only be used with single-threaded asynchronous dispatchers
// and must only be accessed on the dispatch thread since it lacks internal
// synchronization of its state.
//
// Concrete implementations: |async::Wait|, |async::WaitMethod|.
// Please do not create subclasses of WaitBase outside of this library.
class WaitBase {
protected:
    explicit WaitBase(zx_handle_t object, zx_signals_t trigger,
                      async_wait_handler_t* handler);
    ~WaitBase();

    WaitBase(const WaitBase&) = delete;
    WaitBase(WaitBase&&) = delete;
    WaitBase& operator=(const WaitBase&) = delete;
    WaitBase& operator=(WaitBase&&) = delete;

public:
    // Gets or sets the object to wait for signals on.
    zx_handle_t object() const { return wait_.object; }
    void set_object(zx_handle_t object) { wait_.object = object; }

    // Gets or set of signals to wait for.
    zx_signals_t trigger() const { return wait_.trigger; }
    void set_trigger(zx_signals_t trigger) { wait_.trigger = trigger; }

    // Returns true if the wait has begun and not yet completed or been canceled.
    bool is_pending() const { return dispatcher_ != nullptr; }

    // Begins asynchronously waiting for the object to receive one or more of
    // the trigger signals.  Invokes the handler when the wait completes.
    //
    // The wait's handler will be invoked exactly once unless the wait is canceled.
    // When the dispatcher is shutting down (being destroyed), the handlers of
    // all remaining waits will be invoked with a status of |ZX_ERR_CANCELED|.
    //
    // Returns |ZX_OK| if the wait was successfully begun.
    // Returns |ZX_ERR_ACCESS_DENIED| if the object does not have |ZX_RIGHT_WAIT|.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t Begin(async_dispatcher_t* dispatcher);

    // Cancels the wait.
    //
    // If successful, the wait's handler will not run.
    //
    // Returns |ZX_OK| if the wait was pending and it has been successfully
    // canceled; its handler will not run again and can be released immediately.
    // Returns |ZX_ERR_NOT_FOUND| if there was no pending wait either because it
    // already completed, had not been started, or its completion packet has been
    // dequeued from the port and is pending delivery to its handler (perhaps on
    // another thread).
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t Cancel();

protected:
    template <typename T>
    static T* Dispatch(async_wait* wait) {
        static_assert(offsetof(WaitBase, wait_) == 0, "");
        auto self = reinterpret_cast<WaitBase*>(wait);
        self->dispatcher_ = nullptr;
        return static_cast<T*>(self);
    }

private:
    async_wait_t wait_;
    async_dispatcher_t* dispatcher_ = nullptr;
};

// An asynchronous wait whose handler is bound to a |async::Wait::Handler| function.
//
// Prefer using |async::WaitMethod| instead for binding to a fixed class member
// function since it is more efficient to dispatch.
class Wait final : public WaitBase {
public:
    // Handles completion of asynchronous wait operations.
    //
    // The |status| is |ZX_OK| if the wait was satisfied and |signal| is non-null.
    // The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down before
    // the task's handler ran or the task was canceled.
    using Handler = fbl::Function<void(async_dispatcher_t* dispatcher,
                                       async::Wait* wait,
                                       zx_status_t status,
                                       const zx_packet_signal_t* signal)>;

    explicit Wait(zx_handle_t object = ZX_HANDLE_INVALID,
                  zx_signals_t trigger = ZX_SIGNAL_NONE,
                  Handler handler = nullptr);
    ~Wait();

    void set_handler(Handler handler) { handler_ = fbl::move(handler); }
    bool has_handler() const { return !!handler_; }

private:
    static void CallHandler(async_dispatcher_t* dispatcher, async_wait_t* wait,
                            zx_status_t status, const zx_packet_signal_t* signal);

    Handler handler_;
};

// An asynchronous wait whose handler is bound to a fixed class member function.
//
// Usage:
//
// class Foo {
//     void Handle(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
//                 const zx_packet_signal_t* signal) { ... }
//     async::WaitMethod<Foo, &Foo::Handle> wait_{this};
// };
template <class Class,
          void (Class::*method)(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                                const zx_packet_signal_t* signal)>
class WaitMethod final : public WaitBase {
public:
    explicit WaitMethod(Class* instance,
                        zx_handle_t object = ZX_HANDLE_INVALID,
                        zx_signals_t trigger = ZX_SIGNAL_NONE)
        : WaitBase(object, trigger, &WaitMethod::CallHandler),
          instance_(instance) {}
    ~WaitMethod() = default;

private:
    static void CallHandler(async_dispatcher_t* dispatcher, async_wait_t* wait,
                            zx_status_t status, const zx_packet_signal_t* signal) {
        auto self = Dispatch<WaitMethod>(wait);
        (self->instance_->*method)(dispatcher, self, status, signal);
    }

    Class* const instance_;
};

} // namespace async
