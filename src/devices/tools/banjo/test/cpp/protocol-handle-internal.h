// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.handle banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_handle, SynchronousHandleHandle,
        void (C::*)(zx::handle h, zx::handle* out_h, zx::handle* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_process, SynchronousHandleProcess,
        void (C::*)(zx::process h, zx::process* out_h, zx::process* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_thread, SynchronousHandleThread,
        void (C::*)(zx::thread h, zx::thread* out_h, zx::thread* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_vmo, SynchronousHandleVmo,
        void (C::*)(zx::vmo h, zx::vmo* out_h, zx::vmo* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_channel, SynchronousHandleChannel,
        void (C::*)(zx::channel h, zx::channel* out_h, zx::channel* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_event, SynchronousHandleEvent,
        void (C::*)(zx::event h, zx::event* out_h, zx::event* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_port, SynchronousHandlePort,
        void (C::*)(zx::port h, zx::port* out_h, zx::port* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_interrupt, SynchronousHandleInterrupt,
        void (C::*)(zx::interrupt h, zx::interrupt* out_h, zx::interrupt* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_debug_log, SynchronousHandleDebugLog,
        void (C::*)(zx::debuglog h, zx::debuglog* out_h, zx::debuglog* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_socket, SynchronousHandleSocket,
        void (C::*)(zx::socket h, zx::socket* out_h, zx::socket* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_resource, SynchronousHandleResource,
        void (C::*)(zx::resource h, zx::resource* out_h, zx::resource* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_event_pair, SynchronousHandleEventPair,
        void (C::*)(zx::eventpair h, zx::eventpair* out_h, zx::eventpair* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_job, SynchronousHandleJob,
        void (C::*)(zx::job h, zx::job* out_h, zx::job* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_vmar, SynchronousHandleVmar,
        void (C::*)(zx::vmar h, zx::vmar* out_h, zx::vmar* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_fifo, SynchronousHandleFifo,
        void (C::*)(zx::fifo h, zx::fifo* out_h, zx::fifo* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_guest, SynchronousHandleGuest,
        void (C::*)(zx::guest h, zx::guest* out_h, zx::guest* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_timer, SynchronousHandleTimer,
        void (C::*)(zx::timer h, zx::timer* out_h, zx::timer* out_h2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_handle_protocol_profile, SynchronousHandleProfile,
        void (C::*)(zx::profile h, zx::profile* out_h, zx::profile* out_h2));


template <typename D>
constexpr void CheckSynchronousHandleProtocolSubclass() {
    static_assert(internal::has_synchronous_handle_protocol_handle<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleHandle(zx::handle h, zx::handle* out_h, zx::handle* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_process<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleProcess(zx::process h, zx::process* out_h, zx::process* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_thread<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleThread(zx::thread h, zx::thread* out_h, zx::thread* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_vmo<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleVmo(zx::vmo h, zx::vmo* out_h, zx::vmo* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_channel<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleChannel(zx::channel h, zx::channel* out_h, zx::channel* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_event<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleEvent(zx::event h, zx::event* out_h, zx::event* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_port<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandlePort(zx::port h, zx::port* out_h, zx::port* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_interrupt<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleInterrupt(zx::interrupt h, zx::interrupt* out_h, zx::interrupt* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_debug_log<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleDebugLog(zx::debuglog h, zx::debuglog* out_h, zx::debuglog* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_socket<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleSocket(zx::socket h, zx::socket* out_h, zx::socket* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_resource<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleResource(zx::resource h, zx::resource* out_h, zx::resource* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_event_pair<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleEventPair(zx::eventpair h, zx::eventpair* out_h, zx::eventpair* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_job<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleJob(zx::job h, zx::job* out_h, zx::job* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_vmar<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleVmar(zx::vmar h, zx::vmar* out_h, zx::vmar* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_fifo<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleFifo(zx::fifo h, zx::fifo* out_h, zx::fifo* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_guest<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleGuest(zx::guest h, zx::guest* out_h, zx::guest* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_timer<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleTimer(zx::timer h, zx::timer* out_h, zx::timer* out_h2);");

    static_assert(internal::has_synchronous_handle_protocol_profile<D>::value,
        "SynchronousHandleProtocol subclasses must implement "
        "void SynchronousHandleProfile(zx::profile h, zx::profile* out_h, zx::profile* out_h2);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_another_synchronous_handle_protocol_handle, AnotherSynchronousHandleHandle,
        void (C::*)(zx::handle h, zx::handle* out_h, zx::handle* out_h2));


template <typename D>
constexpr void CheckAnotherSynchronousHandleProtocolSubclass() {
    static_assert(internal::has_another_synchronous_handle_protocol_handle<D>::value,
        "AnotherSynchronousHandleProtocol subclasses must implement "
        "void AnotherSynchronousHandleHandle(zx::handle h, zx::handle* out_h, zx::handle* out_h2);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_handle, AsyncHandleHandle,
        void (C::*)(zx::handle h, async_handle_handle_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_process, AsyncHandleProcess,
        void (C::*)(zx::process h, async_handle_process_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_thread, AsyncHandleThread,
        void (C::*)(zx::thread h, async_handle_thread_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_vmo, AsyncHandleVmo,
        void (C::*)(zx::vmo h, async_handle_vmo_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_channel, AsyncHandleChannel,
        void (C::*)(zx::channel h, async_handle_channel_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_event, AsyncHandleEvent,
        void (C::*)(zx::event h, async_handle_event_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_port, AsyncHandlePort,
        void (C::*)(zx::port h, async_handle_port_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_interrupt, AsyncHandleInterrupt,
        void (C::*)(zx::interrupt h, async_handle_interrupt_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_debug_log, AsyncHandleDebugLog,
        void (C::*)(zx::debuglog h, async_handle_debug_log_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_socket, AsyncHandleSocket,
        void (C::*)(zx::socket h, async_handle_socket_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_resource, AsyncHandleResource,
        void (C::*)(zx::resource h, async_handle_resource_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_event_pair, AsyncHandleEventPair,
        void (C::*)(zx::eventpair h, async_handle_event_pair_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_job, AsyncHandleJob,
        void (C::*)(zx::job h, async_handle_job_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_vmar, AsyncHandleVmar,
        void (C::*)(zx::vmar h, async_handle_vmar_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_fifo, AsyncHandleFifo,
        void (C::*)(zx::fifo h, async_handle_fifo_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_guest, AsyncHandleGuest,
        void (C::*)(zx::guest h, async_handle_guest_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_timer, AsyncHandleTimer,
        void (C::*)(zx::timer h, async_handle_timer_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_handle_protocol_profile, AsyncHandleProfile,
        void (C::*)(zx::profile h, async_handle_profile_callback callback, void* cookie));


template <typename D>
constexpr void CheckAsyncHandleProtocolSubclass() {
    static_assert(internal::has_async_handle_protocol_handle<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleHandle(zx::handle h, async_handle_handle_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_process<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleProcess(zx::process h, async_handle_process_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_thread<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleThread(zx::thread h, async_handle_thread_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_vmo<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleVmo(zx::vmo h, async_handle_vmo_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_channel<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleChannel(zx::channel h, async_handle_channel_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_event<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleEvent(zx::event h, async_handle_event_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_port<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandlePort(zx::port h, async_handle_port_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_interrupt<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleInterrupt(zx::interrupt h, async_handle_interrupt_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_debug_log<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleDebugLog(zx::debuglog h, async_handle_debug_log_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_socket<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleSocket(zx::socket h, async_handle_socket_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_resource<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleResource(zx::resource h, async_handle_resource_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_event_pair<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleEventPair(zx::eventpair h, async_handle_event_pair_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_job<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleJob(zx::job h, async_handle_job_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_vmar<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleVmar(zx::vmar h, async_handle_vmar_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_fifo<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleFifo(zx::fifo h, async_handle_fifo_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_guest<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleGuest(zx::guest h, async_handle_guest_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_timer<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleTimer(zx::timer h, async_handle_timer_callback callback, void* cookie);");

    static_assert(internal::has_async_handle_protocol_profile<D>::value,
        "AsyncHandleProtocol subclasses must implement "
        "void AsyncHandleProfile(zx::profile h, async_handle_profile_callback callback, void* cookie);");

}


} // namespace internal
} // namespace ddk
