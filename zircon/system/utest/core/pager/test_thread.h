// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <lib/sync/completion.h>
#include <lib/zx/thread.h>
#include <threads.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

namespace pager_tests {

// Class which executes the specified function on a test thread.
class TestThread {
public:
    explicit TestThread(fbl::Function<bool()> fn);
    ~TestThread();

    // Starts the test thread's execution.
    bool Start();
    // Block until the test thread successfully terminates.
    bool Wait();
    // Block until the test thread terminates with a validation error.
    bool WaitForFailure();
    // Block until the test thread crashes due to an access to |crash_addr|.
    bool WaitForCrash(uintptr_t crash_addr);
    // Block until the test thread is blocked.
    bool WaitForBlocked();
    // Block until the thread terminates.
    bool WaitForTerm() {
        return zx_thread_.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr) == ZX_OK;
    }

    // Kill the test thread.
    bool Kill() {
        killed_ = true;
        return zx_task_kill(zx_thread_.get()) == ZX_OK;
    }

    void Run();

private:
    bool Wait(bool expect_failure, bool expect_crash, uintptr_t crash_addr);
    void PrintDebugInfo(const zx_exception_report_t& report);

    const fbl::Function<bool()> fn_;
    bool killed_ = false;

    thrd_t thrd_;
    zx::thread zx_thread_;
    zx_handle_t port_ = ZX_HANDLE_INVALID;
    bool success_ = false;

    // Makes sure that everything is set up before starting the actual test function.
    sync_completion_t startup_sync_;
};

} // namespace pager_tests
