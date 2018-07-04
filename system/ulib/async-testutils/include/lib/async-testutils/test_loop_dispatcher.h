// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/listnode.h>

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/async/wait.h>
#include <lib/async-testutils/dispatcher_stub.h>

#include <lib/zx/port.h>
#include <lib/zx/time.h>

namespace async {

// A C++ implementation of async_dispatcher_t (to which this class can always be
// upcast), providing an ecapsulation of the dispatch methods at the core of
// |TestLoop|.
class TestLoopDispatcher : public DispatcherStub {
public:
    TestLoopDispatcher();
    ~TestLoopDispatcher();

    // async_dispatcher_t operation implementations.
    zx::time Now() override { return current_time_; };
    zx_status_t BeginWait(async_wait_t* wait) override;
    zx_status_t CancelWait(async_wait_t* wait) override;
    zx_status_t PostTask(async_task_t* task) override;
    zx_status_t CancelTask(async_task_t* task) override;

    // Advances the dispatcher's fake clock to |time|, if |time| is greater
    // than the current time; else, nothing happens.
    void AdvanceTimeTo(zx::time time);

    // Quits the dispatcher. If called while it is running, it will
    // immediately exit and dispatch no further tasks or waits; if called
    // before running, then its next call to run will immediately exit. Further
    // calls to run will dispatch as usual.
    void Quit() { has_quit_ = true; }

    // progressively advancing the fake clock.
    // Returns true iff any tasks or waits were invoked during the run.
    bool RunUntil(zx::time deadline);

private:
    // Returns the deadline of the next posted task if one is pending;
    // else returns zx::time::infinite().
    zx::time GetNextTaskDueTime();

    // Dispatches all tasks which have a due time no later than the current
    // time (unless they are canceled before their handlers have a chance to
    // to run).
    // Returns true iff a task was dispatched.
    bool DispatchPendingTasks();

    // Similarly, dispatches all waits which have already been satisfied (unless
    // they are canceled before their handlers have a chance to run).
    // Returns true iff a wait was dispatched.
    bool DispatchPendingWaits();

    // Moves due tasks from |task_list_| to |due_list_|.
    void ExtractDueTasks();

    // Dispatches all remaining posted waits and tasks.
    void Shutdown();

    // Current fake clock time.
    zx::time current_time_;

    // Pending tasks, earliest deadline first.
    list_node_t task_list_;
    // Due tasks, earliest deadlines first.
    list_node_t due_list_;
    // Pending waits, most recently added first.
    list_node_t wait_list_;

    // Quit state of the dispatcher.
    bool has_quit_ = false;

    // Whether the dispatcher is currently dispatching tasks or waits.
    bool is_dispatching_ = false;

    // Port on which waits are signaled.
    zx::port port_;

    // Wait id used to differentiate waits queued at |port_|.
    uint64_t wait_id_ = 0u;
};

} // namespace async
