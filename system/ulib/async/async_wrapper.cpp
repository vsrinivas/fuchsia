// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/async.h>

namespace async {

Wait::Wait()
    : Wait(MX_HANDLE_INVALID, MX_SIGNAL_NONE) {}

Wait::Wait(mx_handle_t object, mx_signals_t trigger, uint32_t flags)
    : async_wait_t{{ASYNC_STATE_INIT}, &Wait::CallHandler, object, trigger, flags, {}} {}

Wait::~Wait() = default;

mx_status_t Wait::Begin(async_t* async) {
    return async_begin_wait(async, this);
}

mx_status_t Wait::Cancel(async_t* async) {
    return async_cancel_wait(async, this);
}

async_wait_result_t Wait::CallHandler(async_t* async, async_wait_t* wait,
                                      mx_status_t status, const mx_packet_signal_t* signal) {
    return static_cast<Wait*>(wait)->Handle(async, status, signal);
}

Task::Task()
    : Task(MX_TIME_INFINITE, 0u) {}

Task::Task(mx_time_t deadline, uint32_t flags)
    : async_task_t{{ASYNC_STATE_INIT}, &Task::CallHandler, deadline, flags, {}} {}

Task::~Task() = default;

mx_status_t Task::Post(async_t* async) {
    return async_post_task(async, this);
}

mx_status_t Task::Cancel(async_t* async) {
    return async_cancel_task(async, this);
}

async_task_result_t Task::CallHandler(async_t* async, async_task_t* task,
                                      mx_status_t status) {
    return static_cast<Task*>(task)->Handle(async, status);
}

Receiver::Receiver(uint32_t flags)
    : async_receiver_t{{ASYNC_STATE_INIT}, &Receiver::CallHandler, flags, {}} {}

Receiver::~Receiver() = default;

mx_status_t Receiver::Queue(async_t* async, const mx_packet_user_t* data) {
    return async_queue_packet(async, this, data);
}

void Receiver::CallHandler(async_t* async, async_receiver_t* receiver,
                           mx_status_t status, const mx_packet_user_t* data) {
    static_cast<Receiver*>(receiver)->Handle(async, status, data);
}

} // namespace async
