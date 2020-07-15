// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fuchsia-async/cpp/executor.h"

extern "C" void* fasync_executor_create(void* cb_executor);
extern "C" void* fasync_executor_run_singlethreaded(void* executor);
extern "C" void* fasync_executor_quit(void* executor);
extern "C" void fasync_executor_destroy(void* executor);
extern "C" zx_time_t fasync_executor_now(void* executor);
extern "C" zx_status_t fasync_executor_begin_wait(void* executor, async_wait_t* wait);
extern "C" zx_status_t fasync_executor_cancel_wait(void* executor, async_wait_t* wait);
extern "C" zx_status_t fasync_executor_post_task(void* executor, async_task_t* task);
extern "C" zx_status_t fasync_executor_cancel_task(void* executor, async_task_t* task);

namespace fuchsia_async {

const async_ops_t Executor::ops_ = {
    ASYNC_OPS_V1,
    0,
    {now, begin_wait, cancel_wait, post_task, cancel_task, queue_packet, set_guest_bell_trap},
    {nullptr, nullptr, nullptr, nullptr}};

Executor::Executor() : dispatcher_{&ops_}, impl_(fasync_executor_create(this)) {}

Executor::~Executor() { fasync_executor_destroy(impl_); }

void Executor::RunSinglethreaded() { fasync_executor_run_singlethreaded(impl_); }

void Executor::Quit() { fasync_executor_quit(impl_); }

void* Executor::get_impl(async_dispatcher_t* dispatcher) {
  static_assert(offsetof(Executor, dispatcher_) == 0);
  return reinterpret_cast<Executor*>(dispatcher)->impl_;
}

zx_time_t Executor::now(async_dispatcher_t* dispatcher) {
  return fasync_executor_now(get_impl(dispatcher));
}

zx_status_t Executor::begin_wait(async_dispatcher_t* dispatcher, async_wait_t* wait) {
  return fasync_executor_begin_wait(get_impl(dispatcher), wait);
}

zx_status_t Executor::cancel_wait(async_dispatcher_t* dispatcher, async_wait_t* wait) {
  return fasync_executor_cancel_wait(get_impl(dispatcher), wait);
}

zx_status_t Executor::post_task(async_dispatcher_t* dispatcher, async_task_t* task) {
  return fasync_executor_post_task(get_impl(dispatcher), task);
}

zx_status_t Executor::cancel_task(async_dispatcher_t* dispatcher, async_task_t* task) {
  return fasync_executor_cancel_task(get_impl(dispatcher), task);
}

zx_status_t Executor::queue_packet(async_dispatcher_t* dispatcher, async_receiver_t* receiver,
                                   const zx_packet_user_t* data) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Executor::set_guest_bell_trap(async_dispatcher_t* dispatcher,
                                          async_guest_bell_trap_t* trap, zx_handle_t guest,
                                          zx_vaddr_t addr, size_t length) {
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace fuchsia_async
