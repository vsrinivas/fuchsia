// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "test_loop_dispatcher.h"

#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>

namespace async {

// A message loop with a fake clock, to be controlled within a test setting.
class TestLoop {
public:
    TestLoop();
    ~TestLoop();

    async_t* async() { return &dispatcher_; }

    // Returns the current fake clock time.
    zx::time Now() const { return current_time_; }

    // Advances the fake clock by |delta|.
    void AdvanceTimeBy(zx::duration delta) { current_time_ += delta; }

    // Quits the message loop. No due or further tasks and waits will be
    // dispatched upon running.
    void Quit() { has_quit_ = true; }

    // Resets the quit state of the message loop: due tasks and waits will once
    // again be dispatched upon running.
    void ResetQuit() { has_quit_ = false; }

    // Dispatches all waits and all tasks with deadlines up to the current fake
    // clock time.
    // Returns |ZX_OK| if the loop has finished dispatching and is now idle.
    // Returns |ZX_ERR_CANCELED| if the loop has quit.
    zx_status_t RunUntilIdle();

private:
    zx::time current_time_;

    // Encapsulation of the async_t dispatch methods.
    TestLoopDispatcher dispatcher_;

    bool has_quit_ = false;
};

} // namespace async
