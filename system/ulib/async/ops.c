// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/receiver.h>
#include <lib/async/task.h>
#include <lib/async/time.h>
#include <lib/async/trap.h>
#include <lib/async/wait.h>

zx_time_t async_now(async_t* async) {
    return async->ops->now(async);
}

zx_status_t async_begin_wait(async_t* async, async_wait_t* wait) {
    return async->ops->begin_wait(async, wait);
}

zx_status_t async_cancel_wait(async_t* async, async_wait_t* wait) {
    return async->ops->cancel_wait(async, wait);
}

zx_status_t async_post_task(async_t* async, async_task_t* task) {
    return async->ops->post_task(async, task);
}

zx_status_t async_cancel_task(async_t* async, async_task_t* task) {
    return async->ops->cancel_task(async, task);
}

zx_status_t async_queue_packet(async_t* async, async_receiver_t* receiver,
                               const zx_packet_user_t* data) {
    return async->ops->queue_packet(async, receiver, data);
}

zx_status_t async_set_guest_bell_trap(async_t* async, async_guest_bell_trap_t* trap,
                                      zx_handle_t guest, zx_vaddr_t addr, size_t length) {
    return async->ops->set_guest_bell_trap(async, trap, guest, addr, length);
}
