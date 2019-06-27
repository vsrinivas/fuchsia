// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fs/locking.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/fit/promise.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>
#include <thread>

namespace blobfs {

// A generic task executor, capable of running only when work is available until
// destroyed. Tasks added to the BackgroundExecutor are executed on a single thread.
//
// This class is not assignable, copyable, or moveable.
// This class is thread-safe.
class BackgroundExecutor final : public fit::executor {
public:
    BackgroundExecutor();
    BackgroundExecutor(const BackgroundExecutor&) = delete;
    BackgroundExecutor(BackgroundExecutor&&) = delete;
    BackgroundExecutor& operator=(const BackgroundExecutor&) = delete;
    BackgroundExecutor& operator=(BackgroundExecutor&&) = delete;
    ~BackgroundExecutor();

    // Schedules a unit of work to be processed in a background thread.
    //
    // All tasks scheduled to |BackgroundExecutor| via this method are not serialized.
    //
    // Serialization may be enforced by wrapping incoming objects with a fit::sequencer
    // object, if desired.
    void schedule_task(fit::pending_task task) final {
        executor_.schedule_task(std::move(task));
    }

private:
    // Executor which dispatches all scheduled tasks.
    fit::single_threaded_executor executor_;
    // Thread which periodically updates all pending data allocations.
    std::thread thrd_;

    // Protects access to the "terminate_" task.
    //
    // Used infrequently -- only on setup and teardown.
    std::mutex lock_;

    // An "always scheduled" suspended task, which is resumed during destruction
    // to finish running all tasks and then exit.
    fit::suspended_task terminate_ FS_TA_GUARDED(lock_);
    bool should_terminate_ FS_TA_GUARDED(lock_) = false;
};

} //namespace blobfs

