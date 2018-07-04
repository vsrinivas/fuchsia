// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "test_loop_dispatcher.h"

#include <lib/async/dispatcher.h>

namespace async {

// A message loop with a fake clock, to be controlled within a test setting.
class TestLoop {
public:
    TestLoop();
    ~TestLoop();

    async_dispatcher_t* dispatcher() { return &dispatcher_; }
    // TODO(davemoore): ZX-2337 Remove after all external references have been changed
    // to async_dispatcher_t.
    async_dispatcher_t* async() { return dispatcher(); }

    // Returns the current fake clock time.
    zx::time Now() { return dispatcher_.Now(); }

    // Advances the fake clock time to |time|, if |time| is greater than the
    // current time; else, nothing happens.
    void AdvanceTimeTo(zx::time time) { dispatcher_.AdvanceTimeTo(time); }

    // Advances the fake clock time by |delta|.
    void AdvanceTimeBy(zx::duration delta) { AdvanceTimeTo(Now() + delta); }

    // Quits the message loop. If called while running, it will immediately
    // exit and dispatch no further tasks or waits; if called before running,
    // then next call to run will immediately exit. Further calls to run will
    // dispatch as usual.
    void Quit() { dispatcher_.Quit(); }

    // Dispatches all waits and all tasks with deadlines up until |deadline|,
    // progressively advancing the fake clock.
    // Returns true iff any tasks or waits were invoked during the run.
    bool RunUntil(zx::time deadline) { return dispatcher_.RunUntil(deadline); }

    // Dispatches all waits and all tasks with deadlines up until |duration|
    // from the the current time, progressively advancing the fake clock.
    // Returns true iff any tasks or waits were invoked during the run.
    bool RunFor(zx::duration duration) { return RunUntil(Now() + duration); }

    // Dispatches all waits and all tasks with deadlines up until the current
    // time, progressively advancing the fake clock.
    // Returns true iff any tasks or waits were invoked during the run.
    bool RunUntilIdle() { return RunUntil(Now()); }

private:
    // Encapsulation of the async_t dispatch methods.
    TestLoopDispatcher dispatcher_;
};

} // namespace async
