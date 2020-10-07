// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fuchsia-async/cpp/executor.h"

#include <lib/async/receiver.h>
#include <lib/async/task.h>
#include <lib/async/time.h>
#include <lib/async/trap.h>
#include <lib/async/wait.h>

#include <gtest/gtest.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

namespace fuchsia_async {
namespace testing {

TEST(FuchsiaAsync, CanCreateAndExitLoop) { Executor executor; }

TEST(FuchsiaAsync, SomeOperationsAreUnsupported) {
  Executor executor;
  auto* dispatcher = executor.dispatcher();
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_queue_packet(dispatcher, nullptr, nullptr));
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_set_guest_bell_trap(dispatcher, nullptr, 0, 0, 0));
}

template <class F>
void PostTask(Executor* executor, F impl) {
  struct Task {
    async_task_t task;
    F impl;
    static void Handler(async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status) {
      auto* t = reinterpret_cast<Task*>(task);
      t->impl();
      delete t;
    }
  };
  Task* task = new Task{{
                            ASYNC_STATE_INIT,
                            Task::Handler,
                            async_now(executor->dispatcher()),
                        },
                        std::move(impl)};
  EXPECT_EQ(ZX_OK, async_post_task(executor->dispatcher(), &task->task));
}

TEST(FuchsiaAsync, CanPostTask) {
  Executor executor;
  PostTask(&executor, [&executor]() { executor.Quit(); });
  executor.RunSinglethreaded();
}

template <class F>
void PostWait(Executor* executor, zx_handle_t handle, zx_signals_t trigger, F impl) {
  struct Wait {
    async_wait_t wait;
    F impl;
    static void Handler(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
      auto* w = reinterpret_cast<Wait*>(wait);
      w->impl(status, signal);
      delete w;
    }
  };
  Wait* wait = new Wait{{ASYNC_STATE_INIT, Wait::Handler, handle, trigger, 0}, std::move(impl)};
  EXPECT_EQ(ZX_OK, async_begin_wait(executor->dispatcher(), &wait->wait));
}

// TODO: make this available for host also
#ifdef __Fuchsia__
TEST(FuchsiaAsync, CanWaitForRead) {
  Executor executor;
  zx_handle_t a, b;
  ASSERT_EQ(ZX_OK, zx_channel_create(0, &a, &b));
  fprintf(stderr, "made channels %u %u\n", a, b);
  bool write_queued = false;
  PostWait(&executor, b, ZX_CHANNEL_READABLE,
           [&executor, &write_queued](zx_status_t status, const zx_packet_signal_t* signal) {
             EXPECT_EQ(true, write_queued);
             EXPECT_EQ(ZX_OK, status);
             EXPECT_TRUE((signal->observed & ZX_CHANNEL_READABLE) != 0);
             executor.Quit();
           });
  PostTask(&executor, [a, &write_queued]() {
    EXPECT_EQ(false, write_queued);
    fprintf(stderr, "write to channel %u\n", a);
    ASSERT_EQ(ZX_OK, zx_channel_write(a, 0, nullptr, 0, nullptr, 0));
    write_queued = true;
  });
  executor.RunSinglethreaded();
  zx_handle_close(a);
  zx_handle_close(b);
}
#endif

}  // namespace testing
}  // namespace fuchsia_async
