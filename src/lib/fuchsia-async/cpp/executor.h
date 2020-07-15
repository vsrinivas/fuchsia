// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUCHSIA_ASYNC_CPP_EXECUTOR_H_
#define SRC_LIB_FUCHSIA_ASYNC_CPP_EXECUTOR_H_

#include <lib/async/dispatcher.h>

#include <cstddef>

namespace fuchsia_async {

class Executor {
 public:
  Executor();
  ~Executor();

  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  async_dispatcher_t* dispatcher() { return &dispatcher_; }

  void RunSinglethreaded();
  void Quit();

 private:
  static void* get_impl(async_dispatcher_t* dispatcher);

  static zx_time_t now(async_dispatcher_t* dispatcher);
  static zx_status_t begin_wait(async_dispatcher_t* dispatcher, async_wait_t* wait);
  static zx_status_t cancel_wait(async_dispatcher_t* dispatcher, async_wait_t* wait);
  static zx_status_t post_task(async_dispatcher_t* dispatcher, async_task_t* task);
  static zx_status_t cancel_task(async_dispatcher_t* dispatcher, async_task_t* task);
  static zx_status_t queue_packet(async_dispatcher_t* dispatcher, async_receiver_t* receiver,
                                  const zx_packet_user_t* data);
  static zx_status_t set_guest_bell_trap(async_dispatcher_t* dispatcher,
                                         async_guest_bell_trap_t* trap, zx_handle_t guest,
                                         zx_vaddr_t addr, size_t length);

  async_dispatcher_t dispatcher_;
  void* impl_;

  static const async_ops_t ops_;
};

}  // namespace fuchsia_async

#endif  // SRC_LIB_FUCHSIA_ASYNC_CPP_EXECUTOR_H_
