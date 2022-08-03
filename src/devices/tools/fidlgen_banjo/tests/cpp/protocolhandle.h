// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolhandle banjo file

#pragma once

#include <banjo/examples/protocolhandle/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/fifo.h>
#include <lib/zx/guest.h>
#include <lib/zx/handle.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/profile.h>
#include <lib/zx/resource.h>
#include <lib/zx/socket.h>
#include <lib/zx/thread.h>
#include <lib/zx/timer.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK protocolhandle-protocol support
//
// :: Proxies ::
//
// ddk::SynchronousHandleProtocolClient is a simple wrapper around
// synchronous_handle_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::SynchronousHandleProtocol is a mixin class that simplifies writing DDK drivers
// that implement the synchronous-handle protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SYNCHRONOUS_HANDLE device.
// class SynchronousHandleDevice;
// using SynchronousHandleDeviceType = ddk::Device<SynchronousHandleDevice, /* ddk mixins */>;
//
// class SynchronousHandleDevice : public SynchronousHandleDeviceType,
//                      public ddk::SynchronousHandleProtocol<SynchronousHandleDevice> {
//   public:
//     SynchronousHandleDevice(zx_device_t* parent)
//         : SynchronousHandleDeviceType(parent) {}
//
//     void SynchronousHandleHandle(zx::handle h, zx::handle* out_h, zx::handle* out_h2);
//
//     void SynchronousHandleProcess(zx::process h, zx::process* out_h, zx::process* out_h2);
//
//     void SynchronousHandleThread(zx::thread h, zx::thread* out_h, zx::thread* out_h2);
//
//     void SynchronousHandleVmo(zx::vmo h, zx::vmo* out_h, zx::vmo* out_h2);
//
//     void SynchronousHandleChannel(zx::channel h, zx::channel* out_h, zx::channel* out_h2);
//
//     void SynchronousHandleEvent(zx::event h, zx::event* out_h, zx::event* out_h2);
//
//     void SynchronousHandlePort(zx::port h, zx::port* out_h, zx::port* out_h2);
//
//     void SynchronousHandleInterrupt(zx::interrupt h, zx::interrupt* out_h, zx::interrupt* out_h2);
//
//     void SynchronousHandleSocket(zx::socket h, zx::socket* out_h, zx::socket* out_h2);
//
//     void SynchronousHandleResource(zx::resource h, zx::resource* out_h, zx::resource* out_h2);
//
//     void SynchronousHandleEventPair(zx::eventpair h, zx::eventpair* out_h, zx::eventpair* out_h2);
//
//     void SynchronousHandleJob(zx::job h, zx::job* out_h, zx::job* out_h2);
//
//     void SynchronousHandleVmar(zx::vmar h, zx::vmar* out_h, zx::vmar* out_h2);
//
//     void SynchronousHandleFifo(zx::fifo h, zx::fifo* out_h, zx::fifo* out_h2);
//
//     void SynchronousHandleGuest(zx::guest h, zx::guest* out_h, zx::guest* out_h2);
//
//     void SynchronousHandleTimer(zx::timer h, zx::timer* out_h, zx::timer* out_h2);
//
//     void SynchronousHandleProfile(zx::profile h, zx::profile* out_h, zx::profile* out_h2);
//
//     ...
// };
// :: Proxies ::
//
// ddk::AsyncHandleProtocolClient is a simple wrapper around
// async_handle_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::AsyncHandleProtocol is a mixin class that simplifies writing DDK drivers
// that implement the async-handle protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ASYNC_HANDLE device.
// class AsyncHandleDevice;
// using AsyncHandleDeviceType = ddk::Device<AsyncHandleDevice, /* ddk mixins */>;
//
// class AsyncHandleDevice : public AsyncHandleDeviceType,
//                      public ddk::AsyncHandleProtocol<AsyncHandleDevice> {
//   public:
//     AsyncHandleDevice(zx_device_t* parent)
//         : AsyncHandleDeviceType(parent) {}
//
//     void AsyncHandleHandle(zx::handle h, async_handle_handle_callback callback, void* cookie);
//
//     void AsyncHandleProcess(zx::process h, async_handle_process_callback callback, void* cookie);
//
//     void AsyncHandleThread(zx::thread h, async_handle_thread_callback callback, void* cookie);
//
//     void AsyncHandleVmo(zx::vmo h, async_handle_vmo_callback callback, void* cookie);
//
//     void AsyncHandleChannel(zx::channel h, async_handle_channel_callback callback, void* cookie);
//
//     void AsyncHandleEvent(zx::event h, async_handle_event_callback callback, void* cookie);
//
//     void AsyncHandlePort(zx::port h, async_handle_port_callback callback, void* cookie);
//
//     void AsyncHandleInterrupt(zx::interrupt h, async_handle_interrupt_callback callback, void* cookie);
//
//     void AsyncHandleSocket(zx::socket h, async_handle_socket_callback callback, void* cookie);
//
//     void AsyncHandleResource(zx::resource h, async_handle_resource_callback callback, void* cookie);
//
//     void AsyncHandleEventPair(zx::eventpair h, async_handle_event_pair_callback callback, void* cookie);
//
//     void AsyncHandleJob(zx::job h, async_handle_job_callback callback, void* cookie);
//
//     void AsyncHandleVmar(zx::vmar h, async_handle_vmar_callback callback, void* cookie);
//
//     void AsyncHandleFifo(zx::fifo h, async_handle_fifo_callback callback, void* cookie);
//
//     void AsyncHandleGuest(zx::guest h, async_handle_guest_callback callback, void* cookie);
//
//     void AsyncHandleTimer(zx::timer h, async_handle_timer_callback callback, void* cookie);
//
//     void AsyncHandleProfile(zx::profile h, async_handle_profile_callback callback, void* cookie);
//
//     ...
// };
// :: Proxies ::
//
// ddk::AnotherSynchronousHandleProtocolClient is a simple wrapper around
// another_synchronous_handle_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::AnotherSynchronousHandleProtocol is a mixin class that simplifies writing DDK drivers
// that implement the another-synchronous-handle protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ANOTHER_SYNCHRONOUS_HANDLE device.
// class AnotherSynchronousHandleDevice;
// using AnotherSynchronousHandleDeviceType = ddk::Device<AnotherSynchronousHandleDevice, /* ddk mixins */>;
//
// class AnotherSynchronousHandleDevice : public AnotherSynchronousHandleDeviceType,
//                      public ddk::AnotherSynchronousHandleProtocol<AnotherSynchronousHandleDevice> {
//   public:
//     AnotherSynchronousHandleDevice(zx_device_t* parent)
//         : AnotherSynchronousHandleDeviceType(parent) {}
//
//     void AnotherSynchronousHandleHandle(zx::handle h, zx::handle* out_h, zx::handle* out_h2);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class SynchronousHandleProtocol : public Base {
public:
    SynchronousHandleProtocol() {
        internal::CheckSynchronousHandleProtocolSubclass<D>();
        synchronous_handle_protocol_ops_.handle = SynchronousHandleHandle;
        synchronous_handle_protocol_ops_.process = SynchronousHandleProcess;
        synchronous_handle_protocol_ops_.thread = SynchronousHandleThread;
        synchronous_handle_protocol_ops_.vmo = SynchronousHandleVmo;
        synchronous_handle_protocol_ops_.channel = SynchronousHandleChannel;
        synchronous_handle_protocol_ops_.event = SynchronousHandleEvent;
        synchronous_handle_protocol_ops_.port = SynchronousHandlePort;
        synchronous_handle_protocol_ops_.interrupt = SynchronousHandleInterrupt;
        synchronous_handle_protocol_ops_.socket = SynchronousHandleSocket;
        synchronous_handle_protocol_ops_.resource = SynchronousHandleResource;
        synchronous_handle_protocol_ops_.event_pair = SynchronousHandleEventPair;
        synchronous_handle_protocol_ops_.job = SynchronousHandleJob;
        synchronous_handle_protocol_ops_.vmar = SynchronousHandleVmar;
        synchronous_handle_protocol_ops_.fifo = SynchronousHandleFifo;
        synchronous_handle_protocol_ops_.guest = SynchronousHandleGuest;
        synchronous_handle_protocol_ops_.timer = SynchronousHandleTimer;
        synchronous_handle_protocol_ops_.profile = SynchronousHandleProfile;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_SYNCHRONOUS_HANDLE;
            dev->ddk_proto_ops_ = &synchronous_handle_protocol_ops_;
        }
    }

protected:
    synchronous_handle_protocol_ops_t synchronous_handle_protocol_ops_ = {};

private:
    static void SynchronousHandleHandle(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::handle out_h2;
        zx::handle out_h22;
        static_cast<D*>(ctx)->SynchronousHandleHandle(zx::handle(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleProcess(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::process out_h2;
        zx::process out_h22;
        static_cast<D*>(ctx)->SynchronousHandleProcess(zx::process(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleThread(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::thread out_h2;
        zx::thread out_h22;
        static_cast<D*>(ctx)->SynchronousHandleThread(zx::thread(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleVmo(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::vmo out_h2;
        zx::vmo out_h22;
        static_cast<D*>(ctx)->SynchronousHandleVmo(zx::vmo(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleChannel(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::channel out_h2;
        zx::channel out_h22;
        static_cast<D*>(ctx)->SynchronousHandleChannel(zx::channel(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleEvent(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::event out_h2;
        zx::event out_h22;
        static_cast<D*>(ctx)->SynchronousHandleEvent(zx::event(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandlePort(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::port out_h2;
        zx::port out_h22;
        static_cast<D*>(ctx)->SynchronousHandlePort(zx::port(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleInterrupt(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::interrupt out_h2;
        zx::interrupt out_h22;
        static_cast<D*>(ctx)->SynchronousHandleInterrupt(zx::interrupt(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleSocket(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::socket out_h2;
        zx::socket out_h22;
        static_cast<D*>(ctx)->SynchronousHandleSocket(zx::socket(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleResource(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::resource out_h2;
        zx::resource out_h22;
        static_cast<D*>(ctx)->SynchronousHandleResource(zx::resource(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleEventPair(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::eventpair out_h2;
        zx::eventpair out_h22;
        static_cast<D*>(ctx)->SynchronousHandleEventPair(zx::eventpair(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleJob(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::job out_h2;
        zx::job out_h22;
        static_cast<D*>(ctx)->SynchronousHandleJob(zx::job(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleVmar(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::vmar out_h2;
        zx::vmar out_h22;
        static_cast<D*>(ctx)->SynchronousHandleVmar(zx::vmar(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleFifo(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::fifo out_h2;
        zx::fifo out_h22;
        static_cast<D*>(ctx)->SynchronousHandleFifo(zx::fifo(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleGuest(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::guest out_h2;
        zx::guest out_h22;
        static_cast<D*>(ctx)->SynchronousHandleGuest(zx::guest(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleTimer(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::timer out_h2;
        zx::timer out_h22;
        static_cast<D*>(ctx)->SynchronousHandleTimer(zx::timer(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
    static void SynchronousHandleProfile(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::profile out_h2;
        zx::profile out_h22;
        static_cast<D*>(ctx)->SynchronousHandleProfile(zx::profile(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
};

class SynchronousHandleProtocolClient {
public:
    SynchronousHandleProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    SynchronousHandleProtocolClient(const synchronous_handle_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    SynchronousHandleProtocolClient(zx_device_t* parent) {
        synchronous_handle_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_SYNCHRONOUS_HANDLE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    SynchronousHandleProtocolClient(zx_device_t* parent, const char* fragment_name) {
        synchronous_handle_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_SYNCHRONOUS_HANDLE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a SynchronousHandleProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        SynchronousHandleProtocolClient* result) {
        synchronous_handle_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_SYNCHRONOUS_HANDLE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = SynchronousHandleProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a SynchronousHandleProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        SynchronousHandleProtocolClient* result) {
        synchronous_handle_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_SYNCHRONOUS_HANDLE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = SynchronousHandleProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(synchronous_handle_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }

    void Handle(zx::handle h, zx::handle* out_h, zx::handle* out_h2) const {
        ops_->handle(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Process(zx::process h, zx::process* out_h, zx::process* out_h2) const {
        ops_->process(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Thread(zx::thread h, zx::thread* out_h, zx::thread* out_h2) const {
        ops_->thread(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Vmo(zx::vmo h, zx::vmo* out_h, zx::vmo* out_h2) const {
        ops_->vmo(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Channel(zx::channel h, zx::channel* out_h, zx::channel* out_h2) const {
        ops_->channel(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Event(zx::event h, zx::event* out_h, zx::event* out_h2) const {
        ops_->event(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Port(zx::port h, zx::port* out_h, zx::port* out_h2) const {
        ops_->port(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Interrupt(zx::interrupt h, zx::interrupt* out_h, zx::interrupt* out_h2) const {
        ops_->interrupt(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Socket(zx::socket h, zx::socket* out_h, zx::socket* out_h2) const {
        ops_->socket(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Resource(zx::resource h, zx::resource* out_h, zx::resource* out_h2) const {
        ops_->resource(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void EventPair(zx::eventpair h, zx::eventpair* out_h, zx::eventpair* out_h2) const {
        ops_->event_pair(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Job(zx::job h, zx::job* out_h, zx::job* out_h2) const {
        ops_->job(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Vmar(zx::vmar h, zx::vmar* out_h, zx::vmar* out_h2) const {
        ops_->vmar(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Fifo(zx::fifo h, zx::fifo* out_h, zx::fifo* out_h2) const {
        ops_->fifo(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Guest(zx::guest h, zx::guest* out_h, zx::guest* out_h2) const {
        ops_->guest(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Timer(zx::timer h, zx::timer* out_h, zx::timer* out_h2) const {
        ops_->timer(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

    void Profile(zx::profile h, zx::profile* out_h, zx::profile* out_h2) const {
        ops_->profile(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

private:
    const synchronous_handle_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class AsyncHandleProtocol : public Base {
public:
    AsyncHandleProtocol() {
        internal::CheckAsyncHandleProtocolSubclass<D>();
        async_handle_protocol_ops_.handle = AsyncHandleHandle;
        async_handle_protocol_ops_.process = AsyncHandleProcess;
        async_handle_protocol_ops_.thread = AsyncHandleThread;
        async_handle_protocol_ops_.vmo = AsyncHandleVmo;
        async_handle_protocol_ops_.channel = AsyncHandleChannel;
        async_handle_protocol_ops_.event = AsyncHandleEvent;
        async_handle_protocol_ops_.port = AsyncHandlePort;
        async_handle_protocol_ops_.interrupt = AsyncHandleInterrupt;
        async_handle_protocol_ops_.socket = AsyncHandleSocket;
        async_handle_protocol_ops_.resource = AsyncHandleResource;
        async_handle_protocol_ops_.event_pair = AsyncHandleEventPair;
        async_handle_protocol_ops_.job = AsyncHandleJob;
        async_handle_protocol_ops_.vmar = AsyncHandleVmar;
        async_handle_protocol_ops_.fifo = AsyncHandleFifo;
        async_handle_protocol_ops_.guest = AsyncHandleGuest;
        async_handle_protocol_ops_.timer = AsyncHandleTimer;
        async_handle_protocol_ops_.profile = AsyncHandleProfile;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_ASYNC_HANDLE;
            dev->ddk_proto_ops_ = &async_handle_protocol_ops_;
        }
    }

protected:
    async_handle_protocol_ops_t async_handle_protocol_ops_ = {};

private:
    static void AsyncHandleHandle(void* ctx, zx_handle_t h, async_handle_handle_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleHandle(zx::handle(h), callback, cookie);
    }
    static void AsyncHandleProcess(void* ctx, zx_handle_t h, async_handle_process_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleProcess(zx::process(h), callback, cookie);
    }
    static void AsyncHandleThread(void* ctx, zx_handle_t h, async_handle_thread_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleThread(zx::thread(h), callback, cookie);
    }
    static void AsyncHandleVmo(void* ctx, zx_handle_t h, async_handle_vmo_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleVmo(zx::vmo(h), callback, cookie);
    }
    static void AsyncHandleChannel(void* ctx, zx_handle_t h, async_handle_channel_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleChannel(zx::channel(h), callback, cookie);
    }
    static void AsyncHandleEvent(void* ctx, zx_handle_t h, async_handle_event_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleEvent(zx::event(h), callback, cookie);
    }
    static void AsyncHandlePort(void* ctx, zx_handle_t h, async_handle_port_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandlePort(zx::port(h), callback, cookie);
    }
    static void AsyncHandleInterrupt(void* ctx, zx_handle_t h, async_handle_interrupt_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleInterrupt(zx::interrupt(h), callback, cookie);
    }
    static void AsyncHandleSocket(void* ctx, zx_handle_t h, async_handle_socket_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleSocket(zx::socket(h), callback, cookie);
    }
    static void AsyncHandleResource(void* ctx, zx_handle_t h, async_handle_resource_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleResource(zx::resource(h), callback, cookie);
    }
    static void AsyncHandleEventPair(void* ctx, zx_handle_t h, async_handle_event_pair_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleEventPair(zx::eventpair(h), callback, cookie);
    }
    static void AsyncHandleJob(void* ctx, zx_handle_t h, async_handle_job_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleJob(zx::job(h), callback, cookie);
    }
    static void AsyncHandleVmar(void* ctx, zx_handle_t h, async_handle_vmar_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleVmar(zx::vmar(h), callback, cookie);
    }
    static void AsyncHandleFifo(void* ctx, zx_handle_t h, async_handle_fifo_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleFifo(zx::fifo(h), callback, cookie);
    }
    static void AsyncHandleGuest(void* ctx, zx_handle_t h, async_handle_guest_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleGuest(zx::guest(h), callback, cookie);
    }
    static void AsyncHandleTimer(void* ctx, zx_handle_t h, async_handle_timer_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleTimer(zx::timer(h), callback, cookie);
    }
    static void AsyncHandleProfile(void* ctx, zx_handle_t h, async_handle_profile_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncHandleProfile(zx::profile(h), callback, cookie);
    }
};

class AsyncHandleProtocolClient {
public:
    AsyncHandleProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    AsyncHandleProtocolClient(const async_handle_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    AsyncHandleProtocolClient(zx_device_t* parent) {
        async_handle_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_ASYNC_HANDLE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    AsyncHandleProtocolClient(zx_device_t* parent, const char* fragment_name) {
        async_handle_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_ASYNC_HANDLE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a AsyncHandleProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        AsyncHandleProtocolClient* result) {
        async_handle_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_ASYNC_HANDLE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = AsyncHandleProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a AsyncHandleProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        AsyncHandleProtocolClient* result) {
        async_handle_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_ASYNC_HANDLE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = AsyncHandleProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(async_handle_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }

    void Handle(zx::handle h, async_handle_handle_callback callback, void* cookie) const {
        ops_->handle(ctx_, h.release(), callback, cookie);
    }

    void Process(zx::process h, async_handle_process_callback callback, void* cookie) const {
        ops_->process(ctx_, h.release(), callback, cookie);
    }

    void Thread(zx::thread h, async_handle_thread_callback callback, void* cookie) const {
        ops_->thread(ctx_, h.release(), callback, cookie);
    }

    void Vmo(zx::vmo h, async_handle_vmo_callback callback, void* cookie) const {
        ops_->vmo(ctx_, h.release(), callback, cookie);
    }

    void Channel(zx::channel h, async_handle_channel_callback callback, void* cookie) const {
        ops_->channel(ctx_, h.release(), callback, cookie);
    }

    void Event(zx::event h, async_handle_event_callback callback, void* cookie) const {
        ops_->event(ctx_, h.release(), callback, cookie);
    }

    void Port(zx::port h, async_handle_port_callback callback, void* cookie) const {
        ops_->port(ctx_, h.release(), callback, cookie);
    }

    void Interrupt(zx::interrupt h, async_handle_interrupt_callback callback, void* cookie) const {
        ops_->interrupt(ctx_, h.release(), callback, cookie);
    }

    void Socket(zx::socket h, async_handle_socket_callback callback, void* cookie) const {
        ops_->socket(ctx_, h.release(), callback, cookie);
    }

    void Resource(zx::resource h, async_handle_resource_callback callback, void* cookie) const {
        ops_->resource(ctx_, h.release(), callback, cookie);
    }

    void EventPair(zx::eventpair h, async_handle_event_pair_callback callback, void* cookie) const {
        ops_->event_pair(ctx_, h.release(), callback, cookie);
    }

    void Job(zx::job h, async_handle_job_callback callback, void* cookie) const {
        ops_->job(ctx_, h.release(), callback, cookie);
    }

    void Vmar(zx::vmar h, async_handle_vmar_callback callback, void* cookie) const {
        ops_->vmar(ctx_, h.release(), callback, cookie);
    }

    void Fifo(zx::fifo h, async_handle_fifo_callback callback, void* cookie) const {
        ops_->fifo(ctx_, h.release(), callback, cookie);
    }

    void Guest(zx::guest h, async_handle_guest_callback callback, void* cookie) const {
        ops_->guest(ctx_, h.release(), callback, cookie);
    }

    void Timer(zx::timer h, async_handle_timer_callback callback, void* cookie) const {
        ops_->timer(ctx_, h.release(), callback, cookie);
    }

    void Profile(zx::profile h, async_handle_profile_callback callback, void* cookie) const {
        ops_->profile(ctx_, h.release(), callback, cookie);
    }

private:
    const async_handle_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class AnotherSynchronousHandleProtocol : public Base {
public:
    AnotherSynchronousHandleProtocol() {
        internal::CheckAnotherSynchronousHandleProtocolSubclass<D>();
        another_synchronous_handle_protocol_ops_.handle = AnotherSynchronousHandleHandle;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_ANOTHER_SYNCHRONOUS_HANDLE;
            dev->ddk_proto_ops_ = &another_synchronous_handle_protocol_ops_;
        }
    }

protected:
    another_synchronous_handle_protocol_ops_t another_synchronous_handle_protocol_ops_ = {};

private:
    static void AnotherSynchronousHandleHandle(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
        zx::handle out_h2;
        zx::handle out_h22;
        static_cast<D*>(ctx)->AnotherSynchronousHandleHandle(zx::handle(h), &out_h2, &out_h22);
        *out_h = out_h2.release();
        *out_h2 = out_h22.release();
    }
};

class AnotherSynchronousHandleProtocolClient {
public:
    AnotherSynchronousHandleProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    AnotherSynchronousHandleProtocolClient(const another_synchronous_handle_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    AnotherSynchronousHandleProtocolClient(zx_device_t* parent) {
        another_synchronous_handle_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_ANOTHER_SYNCHRONOUS_HANDLE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    AnotherSynchronousHandleProtocolClient(zx_device_t* parent, const char* fragment_name) {
        another_synchronous_handle_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_ANOTHER_SYNCHRONOUS_HANDLE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a AnotherSynchronousHandleProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        AnotherSynchronousHandleProtocolClient* result) {
        another_synchronous_handle_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_ANOTHER_SYNCHRONOUS_HANDLE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = AnotherSynchronousHandleProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a AnotherSynchronousHandleProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        AnotherSynchronousHandleProtocolClient* result) {
        another_synchronous_handle_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_ANOTHER_SYNCHRONOUS_HANDLE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = AnotherSynchronousHandleProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(another_synchronous_handle_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }

    void Handle(zx::handle h, zx::handle* out_h, zx::handle* out_h2) const {
        ops_->handle(ctx_, h.release(), out_h->reset_and_get_address(), out_h2->reset_and_get_address());
    }

private:
    const another_synchronous_handle_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
