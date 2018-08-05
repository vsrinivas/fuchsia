// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/dispatcher_stub.h>

namespace async {

namespace {

zx_time_t stub_now(async_dispatcher_t* dispatcher) {
    return (static_cast<DispatcherStub*>(dispatcher)->Now()).get();
}

zx_status_t stub_begin_wait(async_dispatcher_t* dispatcher, async_wait_t* wait) {
    return static_cast<DispatcherStub*>(dispatcher)->BeginWait(wait);
}

zx_status_t stub_cancel_wait(async_dispatcher_t* dispatcher, async_wait_t* wait) {
    return static_cast<DispatcherStub*>(dispatcher)->CancelWait(wait);
}

zx_status_t stub_post_task(async_dispatcher_t* dispatcher, async_task_t* task) {
    return static_cast<DispatcherStub*>(dispatcher)->PostTask(task);
}

zx_status_t stub_cancel_task(async_dispatcher_t* dispatcher, async_task_t* task) {
    return static_cast<DispatcherStub*>(dispatcher)->CancelTask(task);
}

zx_status_t stub_queue_packet(async_dispatcher_t* dispatcher, async_receiver_t* receiver,
                              const zx_packet_user_t* data) {
    return static_cast<DispatcherStub*>(dispatcher)->QueuePacket(receiver, data);
}

zx_status_t stub_set_guest_bell_trap(async_dispatcher_t* dispatcher, async_guest_bell_trap_t* trap,
                                     zx_handle_t guest, zx_vaddr_t addr, size_t length) {
    return static_cast<DispatcherStub*>(dispatcher)->SetGuestBellTrap(
        trap, *zx::unowned_guest(guest), addr, length);
}

zx_status_t stub_bind_exception_port(async_dispatcher_t* dispatcher,
                                     async_exception_t* exception) {
    return static_cast<DispatcherStub*>(dispatcher)->BindExceptionPort(exception);
}

zx_status_t stub_unbind_exception_port(async_dispatcher_t* dispatcher,
                                       async_exception_t* exception) {
    return static_cast<DispatcherStub*>(dispatcher)->UnbindExceptionPort(exception);
}

const async_ops_t g_stub_ops = {
    .version = ASYNC_OPS_V2,
    .reserved = 0,
    .v1 = {
        .now = stub_now,
        .begin_wait = stub_begin_wait,
        .cancel_wait = stub_cancel_wait,
        .post_task = stub_post_task,
        .cancel_task = stub_cancel_task,
        .queue_packet = stub_queue_packet,
        .set_guest_bell_trap = stub_set_guest_bell_trap,
    },
    .v2 = {
        .bind_exception_port = stub_bind_exception_port,
        .unbind_exception_port = stub_unbind_exception_port,
    },
};

} // namespace

DispatcherStub::DispatcherStub()
    : async_dispatcher_t{&g_stub_ops} {}

DispatcherStub::~DispatcherStub() {}

zx::time DispatcherStub::Now() {
    return zx::time(0);
}

zx_status_t DispatcherStub::BeginWait(async_wait_t* wait) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DispatcherStub::CancelWait(async_wait_t* wait) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DispatcherStub::PostTask(async_task_t* task) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DispatcherStub::CancelTask(async_task_t* task) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DispatcherStub::QueuePacket(async_receiver_t* receiver,
                                   const zx_packet_user_t* data) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DispatcherStub::SetGuestBellTrap(async_guest_bell_trap_t* trap,
                                        const zx::guest& guest,
                                        zx_vaddr_t addr, size_t length) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DispatcherStub::BindExceptionPort(async_exception_t* exception) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DispatcherStub::UnbindExceptionPort(async_exception_t* exception) {
    return ZX_ERR_NOT_SUPPORTED;
}

} // namespace async
