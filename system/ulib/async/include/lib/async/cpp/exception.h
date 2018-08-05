// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <lib/async/exception.h>

namespace async {

// Holds content for an exception packet receiver and its handler.
//
// After successfully binding the port, the client is responsible for
// retaining the structure in memory (and unmodified) until all packets have
// been received by the handler or the dispatcher shuts down.
//
// Multiple packets may be delivered to the same receiver concurrently.
//
// Concrete implementations: |async::Exception|, |async::ExceptionMethod|.
// Please do not create subclasses of ExceptionBase outside of this library.
class ExceptionBase {
protected:
    ExceptionBase(zx_handle_t task, uint32_t options,
                  async_exception_handler_t* handler);
    ~ExceptionBase();

    ExceptionBase(const ExceptionBase&) = delete;
    ExceptionBase(ExceptionBase&&) = delete;
    ExceptionBase& operator=(const ExceptionBase&) = delete;
    ExceptionBase& operator=(ExceptionBase&&) = delete;

    template <typename T>
    static T* Dispatch(async_exception_t* exception) {
        static_assert(offsetof(ExceptionBase, exception_) == 0, "");
        auto self = reinterpret_cast<ExceptionBase*>(exception);
        return static_cast<T*>(self);
    }

public:
    // Return true if task's exception port has been bound to.
    bool is_bound() const { return !!dispatcher_; }

    // Bind the async port to the task's exception port.
    //
    // Returns |ZX_OK| if the task's exception port is successfully bound to.
    // Returns |ZX_ERR_ALREADY_EXISTS| if port is already bound.
    // See |zx_task_bind_exception_port()| for other possible errors.
    zx_status_t Bind(async_dispatcher_t* dispatcher);

    // Unbind the async port from the task's exception port.
    //
    // Returns |ZX_OK| if the task's exception port is successfully unbound.
    // Returns ZX_ERR_NOT_FOUND if the port is not bound.
    // See |zx_task_bind_exception_port()| for other possible errors.
    zx_status_t Unbind();

private:
    async_exception_t exception_;
    async_dispatcher_t* dispatcher_ = nullptr;
};

// A receiver whose handler is bound to a |async::Task::Handler| function.
//
// Prefer using |async::ExceptioniReceiverMethod| instead for binding to a fixed class member
// function since it is more efficient to dispatch.
class Exception final : public ExceptionBase {
public:
    // Handles receipt of packets containing exception reports.
    //
    // The |status| is |ZX_OK| if the packet was successfully delivered and |data|
    // contains the information from the packet, otherwise |data| is null.
    using Handler = fbl::Function<void(async_dispatcher_t* dispatcher,
                                       async::Exception* exception,
                                       zx_status_t status,
                                       const zx_port_packet_t* report)>;

    Exception(zx_handle_t task, uint32_t options, Handler handler);
    ~Exception();

private:
    static void CallHandler(async_dispatcher_t* dispatcher,
                            async_exception_t* exception,
                            zx_status_t status, const zx_port_packet_t* report);

    Handler handler_;
};

// A receiver whose handler is bound to a fixed class member function.
//
// Usage:
//
// class Foo {
//     void Handle(async_dispatcher_t* dispatcher, async::ExceptionBase* receiver, zx_status_t status,
//                 const zx_port_packet_t* report) { ... }
//     async::ExceptionReceiverMethod<Foo, &Foo::Handle> receiver_{this};
// };
template <class Class,
          void (Class::*method)(async_dispatcher_t* dispatcher, async::ExceptionBase* receiver,
                                zx_status_t status, const zx_port_packet_t* report)>
class ExceptionMethod final : public ExceptionBase {
public:
    ExceptionMethod(Class* instance,
                            zx_handle_t task, uint32_t options)
        : ExceptionBase(task, options, &ExceptionMethod::CallHandler),
          instance_(instance) {}

private:
    static void CallHandler(async_dispatcher_t* dispatcher,
                            async_exception_t* exception,
                            zx_status_t status, const zx_port_packet_t* report) {
        auto self = Dispatch<ExceptionMethod>(exception);
        (self->instance_->*method)(dispatcher, self, status, report);
    }

    Class* const instance_;
};

} // namespace async
