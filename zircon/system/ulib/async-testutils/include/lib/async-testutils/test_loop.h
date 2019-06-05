// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_TESTUTILS_TEST_LOOP_H_
#define LIB_ASYNC_TESTUTILS_TEST_LOOP_H_

#include <memory>
#include <vector>

#include <lib/async-testutils/test_loop_dispatcher.h>

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
    // Constructs a TestLoop with a seed from the environment, or a random
    // seed if absent.
    TestLoop();
    // If state is nonzero, constructs a TestLoop with the given seed.
    // Otherwise, uses a seed from the environment or a random seed.
    TestLoop(uint32_t state);
    ~TestLoop();

    // Returns the test loop's asynchronous dispatcher.
    async_dispatcher_t* dispatcher();

    // Returns a loop interface simulating the starting up of a new message
    // loop. The lifetime of the 'loop' is tied to the returned interface.
    // Each successive calls to this method corresponds to a new loop.
    std::unique_ptr<LoopInterface> StartNewLoop();

    // Returns the current fake clock time.
    zx::time Now() const;

    // Quits the message loop. If called while running, it will immediately
    // exit and dispatch no further tasks or waits; if called before running,
    // then next call to run will immediately exit. Further calls to run will
    // dispatch as usual.
    void Quit();

    // Advances the fake clock time by the smallest possible amount.
    // This doesn't run the loop.
    void AdvanceTimeByEpsilon();

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

    // The initial value of the state of the TestLoop.
    uint32_t initial_state() { return initial_state_; }

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

    std::unique_ptr<TestLoopTimeKeeper> time_keeper_;

    // Encapsulation of the async_dispatcher_t dispatch methods.
    std::vector<std::unique_ptr<TestLoopDispatcher>> dispatchers_;

    // The seed of a pseudo-random number used to determinisitically determine the
    // dispatching order across |dispatchers_|.
    uint32_t initial_state_;
    // The current state of the pseudo-random generator.
    uint32_t state_;

    // Quit state of the loop.
    bool has_quit_ = false;
    // Whether the loop is currently running.
    bool is_running_ = false;
};

} // namespace async

#endif // LIB_ASYNC_TESTUTILS_TEST_LOOP_H_
