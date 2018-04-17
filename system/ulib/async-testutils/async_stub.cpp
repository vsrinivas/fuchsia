// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/async_stub.h>

namespace async {

namespace {

zx_time_t stub_now(async_t* async) {
    return (static_cast<AsyncStub*>(async)->Now()).get();
}

zx_status_t stub_begin_wait(async_t* async, async_wait_t* wait) {
    return static_cast<AsyncStub*>(async)->BeginWait(wait);
}

zx_status_t stub_cancel_wait(async_t* async, async_wait_t* wait) {
    return static_cast<AsyncStub*>(async)->CancelWait(wait);
}

zx_status_t stub_post_task(async_t* async, async_task_t* task) {
    return static_cast<AsyncStub*>(async)->PostTask(task);
}

zx_status_t stub_cancel_task(async_t* async, async_task_t* task) {
    return static_cast<AsyncStub*>(async)->CancelTask(task);
}

zx_status_t stub_queue_packet(async_t* async, async_receiver_t* receiver,
                              const zx_packet_user_t* data) {
    return static_cast<AsyncStub*>(async)->QueuePacket(receiver, data);
}

zx_status_t stub_set_guest_bell_trap(async_t* async, async_guest_bell_trap_t* trap,
                                     zx_handle_t guest, zx_vaddr_t addr, size_t length) {
    return static_cast<AsyncStub*>(async)->SetGuestBellTrap(
        trap, zx::unowned_guest::wrap(guest), addr, length);
}

const async_ops_t g_stub_ops = {
    .version = ASYNC_OPS_V1,
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
};

} // namespace

AsyncStub::AsyncStub()
    : async_t{&g_stub_ops} {}

AsyncStub::~AsyncStub() {}

zx::time AsyncStub::Now() {
    return zx::time(0);
}

zx_status_t AsyncStub::BeginWait(async_wait_t* wait) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AsyncStub::CancelWait(async_wait_t* wait) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AsyncStub::PostTask(async_task_t* task) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AsyncStub::CancelTask(async_task_t* task) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AsyncStub::QueuePacket(async_receiver_t* receiver,
                                   const zx_packet_user_t* data) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AsyncStub::SetGuestBellTrap(async_guest_bell_trap_t* trap,
                                        const zx::guest& guest,
                                        zx_vaddr_t addr, size_t length) {
    return ZX_ERR_NOT_SUPPORTED;
}

} // namespace async
