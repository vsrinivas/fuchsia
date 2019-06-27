// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/background-executor.h>

#include <zircon/assert.h>

namespace blobfs {

BackgroundExecutor::~BackgroundExecutor() {
    if (thrd_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(lock_);
            // If the "always running" task had suspended, this completes it.
            terminate_.reset();
            // If the "always running" task has not suspened, this advises it to
            // shut itself down.
            should_terminate_ = true;
        }
        thrd_.join();
    }
}

BackgroundExecutor::BackgroundExecutor() {
    // Create a unit of work for the runner to munch on if no tasks are pending.
    // This will ensure the invocation of |executor::run()| doesn't terminate until
    // the termination task is resumed.
    //
    // Once the termination task is resumed, all pending tasks will be completed, and
    // the runner thread will exit.
    auto work = fit::make_promise([this](fit::context& context) mutable
                                  -> fit::result<> {
        std::lock_guard<std::mutex> lock(lock_);
        if (should_terminate_) {
            // In this case, the BackgroundExecutor terminated before the runner started
            // processing this unit of work. That's a quick shutdown!
            //
            // In this case, no one will try to resume us if we suspend, so just
            // exit early.
            return fit::ok();
        }

        // Suspend the task, never to actually return. When the BackgroundExecutor
        // destructor runs, this suspended task will be destroyed.
        terminate_ = context.suspend_task();
        return fit::pending();
    });
    executor_.schedule_task(std::move(work));
    thrd_ = std::thread([this] {
        executor_.run();
    });
}

} // namespace blobfs
