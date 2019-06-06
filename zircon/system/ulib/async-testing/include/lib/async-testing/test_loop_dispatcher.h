// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_TESTING_TEST_LOOP_DISPATCHER_H_
#define LIB_ASYNC_TESTING_TEST_LOOP_DISPATCHER_H_

#include <list>
#include <memory>
#include <set>

#include <lib/async-testing/dispatcher_stub.h>
#include <lib/async-testing/time-keeper.h>
#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/async/wait.h>
#include <lib/zx/port.h>
#include <lib/zx/time.h>

namespace async {

// An asynchronous dispatcher with an abstracted sense of time, controlled by an
// external time-keeping object, for use in testing.
class TestLoopDispatcher : public DispatcherStub {
public:
    TestLoopDispatcher(TimeKeeper* time_keeper);
    ~TestLoopDispatcher();
    TestLoopDispatcher(const TestLoopDispatcher&) = delete;
    TestLoopDispatcher& operator=(const TestLoopDispatcher&) = delete;

    // async_dispatcher_t operation implementations.
    zx::time Now() override;
    zx_status_t BeginWait(async_wait_t* wait) override;
    zx_status_t CancelWait(async_wait_t* wait) override;
    zx_status_t PostTask(async_task_t* task) override;
    zx_status_t CancelTask(async_task_t* task) override;

    // Dispatches the next due task or wait. Returns true iff a message was
    // dispatched.
    bool DispatchNextDueMessage();

    // Whether there are any due tasks or waits.
    bool HasPendingWork();

    // Returns the deadline of the next posted task if one is pending; else
    // returns zx::time::infinite().
    zx::time GetNextTaskDueTime();

private:
    class Activated;
    class TaskActivated;
    class WaitActivated;

    class AsyncTaskComparator {
    public:
        bool operator()(async_task_t* t1, async_task_t* t2) const { return t1->deadline < t2->deadline; }
    };

    // Extracts activated tasks and waits to |activated_|.
    void ExtractActivated();

    // Removes the given task or wait from |activables_| and |activated_|.
    zx_status_t CancelActivatedTaskOrWait(void* task_or_wait);

    // Dispatches all remaining posted waits and tasks, invoking their handlers
    // with status ZX_ERR_CANCELED.
    void Shutdown();

    // A reference to an external object that manages the current time.
    TimeKeeper* const time_keeper_;

    // Whether the loop is shutting down.
    bool in_shutdown_ = false;
    // Pending tasks activable in the future.
    // The ordering of the set is based on the task timeline. Multiple tasks
    // with the same deadline will be equivalent, and be ordered by order of
    // insertion.
    std::multiset<async_task_t*, AsyncTaskComparator> future_tasks_;
    // Pending waits.
    std::set<async_wait_t*> pending_waits_;
    // Activated elements, ready to be dispatched.
    std::list<std::unique_ptr<Activated>> activated_;
    // Port used to register waits.
    zx::port port_;
};

} // namespace async

#endif // LIB_ASYNC_TESTING_TEST_LOOP_DISPATCHER_H_
