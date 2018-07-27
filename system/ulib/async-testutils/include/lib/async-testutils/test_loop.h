// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async-testutils/test_loop_dispatcher.h>

#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/async/dispatcher.h>

namespace async {

// A minimal, abstract message loop interface.
class LoopInterface {
public:
    virtual ~LoopInterface() = default;
    virtual async_dispatcher_t* dispatcher() = 0;
};


// A message loop with a fake clock, to be controlled within a test setting.
class TestLoop {
public:
    TestLoop();
    ~TestLoop();

    // Returns the test loop's asynchronous dispatcher.
    async_dispatcher_t* dispatcher();

    // Returns a loop interface simulating the starting up of a new message
    // loop. The lifetime of the 'loop' is tied to the returned interface.
    // Each successive calls to this method corresponds to a new loop.
    fbl::unique_ptr<LoopInterface> StartNewLoop();

    // Returns the current fake clock time.
    zx::time Now() const;

    // Advances the fake clock time to |time|, if |time| is greater than the
    // current time; else, nothing happens.
    void AdvanceTimeTo(zx::time time);

    // Advances the fake clock time by |delta|.
    void AdvanceTimeBy(zx::duration delta);

    // Quits the message loop. If called while running, it will immediately
    // exit and dispatch no further tasks or waits; if called before running,
    // then next call to run will immediately exit. Further calls to run will
    // dispatch as usual.
    void Quit();

    // Dispatches all waits and all tasks with deadlines up until |deadline|,
    // progressively advancing the fake clock.
    // Returns true iff any tasks or waits were invoked during the run.
    bool RunUntil(zx::time deadline);

    // Dispatches all waits and all tasks with deadlines up until |duration|
    // from the the current time, progressively advancing the fake clock.
    // Returns true iff any tasks or waits were invoked during the run.
    bool RunFor(zx::duration duration);

    // Dispatches all waits and all tasks with deadlines up until the current
    // time, progressively advancing the fake clock.
    // Returns true iff any tasks or waits were invoked during the run.
    bool RunUntilIdle();

private:
    // An implementation of LoopInterface.
    class TestLoopInterface;

    // A TimeKeeper implementation that manages the test loop's fake clock time
    // and fake timers.
    class TestLoopTimeKeeper;

    // Whether there are any due tasks or waits across |dispatchers_|.
    bool HasPendingWork();

    // Returns the next due task time across |dispatchers_|.
    zx::time GetNextTaskDueTime() const;

    fbl::unique_ptr<TestLoopTimeKeeper> time_keeper_;

    // Encapsulation of the async_dispatcher_t dispatch methods.
    fbl::Vector<fbl::unique_ptr<TestLoopDispatcher>> dispatchers_;

    // A pseudo-random number used to determinisitically determine the
    // dispatching order across |dispatchers_|.
    uint32_t state_;

    // Quit state of the loop.
    bool has_quit_ = false;
    // Whether the loop is currently running.
    bool is_running_ = false;
};

} // namespace async
