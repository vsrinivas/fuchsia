// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fuchsia-async/cpp/executor.h"

#include <lib/async/receiver.h>
#include <lib/async/task.h>
#include <lib/async/time.h>
#include <lib/async/trap.h>

#include <gtest/gtest.h>

namespace fuchsia_async {
namespace testing {

TEST(FuchsiaAsync, CanCreateAndExitLoop) { Executor executor; }

TEST(FuchsiaAsync, SomeOperationsAreUnsupported) {
  Executor executor;
  auto* dispatcher = executor.dispatcher();
  EXPECT_EQ(async_queue_packet(dispatcher, nullptr, nullptr), ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(async_set_guest_bell_trap(dispatcher, nullptr, 0, 0, 0), ZX_ERR_NOT_SUPPORTED);
}

template <class F>
void PostTask(Executor* executor, F impl) {
  struct Task {
    async_task_t task;
    F impl;
    static void Handler(async_dispatcher_t* dispatcher, async_task_t* task,
                                   zx_status_t status) {
        Task* t = reinterpret_cast<Task*>(task);
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
  EXPECT_EQ(async_post_task(executor->dispatcher(), &task->task), ZX_OK);
}

TEST(FuchsiaAsync, CanPostTask) {
  Executor executor;
  PostTask(&executor, [&executor]() { executor.Quit(); });
  executor.RunSinglethreaded();
}

}  // namespace testing
}  // namespace fuchsia_async
