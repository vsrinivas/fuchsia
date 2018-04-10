// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <lib/async/receiver.h>

namespace async {

// Holds content for a packet receiver and its handler.
//
// After successfully queuing packets to the receiver, the client is responsible
// for retaining the structure in memory (and unmodified) until all packets have
// been received by the handler or the dispatcher shuts down.  There is no way
// to cancel a packet which has been queued.
//
// Multiple packets may be delivered to the same receiver concurrently.
//
// Concrete implementations: |async::Receiver|, |async::ReceiverMethod|.
// Please do not create subclasses of ReceiverBase outside of this library.
class ReceiverBase {
protected:
    explicit ReceiverBase(async_receiver_handler_t* handler);
    ~ReceiverBase();

    ReceiverBase(const ReceiverBase&) = delete;
    ReceiverBase(ReceiverBase&&) = delete;
    ReceiverBase& operator=(const ReceiverBase&) = delete;
    ReceiverBase& operator=(ReceiverBase&&) = delete;

public:
    // Enqueues a packet of data for delivery to a receiver.
    //
    // The |data| will be copied into the packet.  May be NULL to create a
    // zero-initialized packet payload.
    //
    // Returns |ZX_OK| if the packet was successfully enqueued.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t QueuePacket(async_t* async, const zx_packet_user_t* data = nullptr);

protected:
    template <typename T>
    static T* Dispatch(async_receiver_t* receiver) {
        static_assert(offsetof(ReceiverBase, receiver_) == 0, "");
        auto self = reinterpret_cast<ReceiverBase*>(receiver);
        return static_cast<T*>(self);
    }

private:
    async_receiver_t receiver_;
};

// A receiver whose handler is bound to a |async::Task::Handler| function.
//
// Prefer using |async::ReceiverMethod| instead for binding to a fixed class member
// function since it is more efficient to dispatch.
class Receiver final : public ReceiverBase {
public:
    // Handles receipt of packets containing user supplied data.
    //
    // The |status| is |ZX_OK| if the packet was successfully delivered and |data|
    // contains the information from the packet, otherwise |data| is null.
    using Handler = fbl::Function<void(async_t* async,
                                       async::Receiver* receiver,
                                       zx_status_t status,
                                       const zx_packet_user_t* data)>;

    explicit Receiver(Handler handler = nullptr);
    ~Receiver();

    void set_handler(Handler handler) { handler_ = fbl::move(handler); }
    bool has_handler() const { return !!handler_; }

private:
    static void CallHandler(async_t* async, async_receiver_t* receiver,
                            zx_status_t status, const zx_packet_user_t* data);

    Handler handler_;
};

// A receiver whose handler is bound to a fixed class member function.
//
// Usage:
//
// class Foo {
//     void Handle(async_t* async, async::ReceiverBase* receiver, zx_status_t status,
//                 const zx_packet_user_t* data) { ... }
//     async::ReceiverMethod<Foo, &Foo::Handle> receiver_{this};
// };
template <class Class,
          void (Class::*method)(async_t* async, async::ReceiverBase* receiver,
                                zx_status_t status, const zx_packet_user_t* data)>
class ReceiverMethod final : public ReceiverBase {
public:
    explicit ReceiverMethod(Class* instance)
        : ReceiverBase(&ReceiverMethod::CallHandler), instance_(instance) {}

private:
    static void CallHandler(async_t* async, async_receiver_t* receiver,
                            zx_status_t status, const zx_packet_user_t* data) {
        auto self = Dispatch<ReceiverMethod>(receiver);
        (self->instance_->*method)(async, self, status, data);
    }

    Class* const instance_;
};

} // namespace async
