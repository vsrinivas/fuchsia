// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/journal/background_executor.h"

#include <threads.h>
#include <zircon/assert.h>
#include <zircon/threads.h>

namespace fs {

BackgroundExecutor::~BackgroundExecutor() {
  {
    std::lock_guard lock(lock_);
    // If the "always running" task had suspended, this completes it.
    terminate_.reset();
    // If the "always running" task has not suspended, this advises it to shut itself down.
    should_terminate_ = true;
  }
  int result = thrd_join(thrd_, nullptr);
  ZX_DEBUG_ASSERT(result == thrd_success);
}

BackgroundExecutor::BackgroundExecutor() {
  // Create a unit of work for the runner to munch on if no tasks are pending. This will ensure
  // the invocation of |executor::run()| doesn't terminate until the termination task is resumed.
  //
  // Once the termination task is resumed, all pending tasks will be completed, and the runner
  // thread will exit.
  auto work =
      fpromise::make_promise([this](fpromise::context& context) mutable -> fpromise::result<> {
        std::lock_guard lock(lock_);
        if (should_terminate_) {
          // In this case, the BackgroundExecutor terminated before the runner started processing
          // this unit of work. That's a quick shutdown!
          //
          // In this case, no one will try to resume us if we suspend, so just exit early.
          return fpromise::ok();
        }

        // Suspend the task, never to actually return. When the BackgroundExecutor destructor runs,
        // this suspended task will be destroyed.
        terminate_ = context.suspend_task();
        return fpromise::pending();
      });
  executor_.schedule_task(std::move(work));
  int rc = thrd_create_with_name(
      &thrd_,
      [](void* ctx) {
        reinterpret_cast<BackgroundExecutor*>(ctx)->executor_.run();
        return 0;
      },
      this, "journal-thread");
  ZX_ASSERT_MSG(rc == thrd_success, "Failed to create journal background executor thread: %d", rc);
}

}  // namespace fs
