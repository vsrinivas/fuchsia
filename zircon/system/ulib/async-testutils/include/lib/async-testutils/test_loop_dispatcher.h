// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <lib/async-testutils/dispatcher_stub.h>
#include <lib/async-testutils/time-keeper.h>
#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/async/wait.h>
#include <lib/zx/port.h>
#include <lib/zx/time.h>
#include <zircon/listnode.h>

namespace async {

// An asynchronous dispatcher with an abstracted sense of time, controlled by an
// external time-keeping object, for use in testing.
class TestLoopDispatcher : public DispatcherStub, public TimerDispatcher {
public:
    TestLoopDispatcher(TimeKeeper* time_keeper);
    ~TestLoopDispatcher();
    DISALLOW_COPY_ASSIGN_AND_MOVE(TestLoopDispatcher);

    // async_dispatcher_t operation implementations.
    zx::time Now() override;
    zx_status_t BeginWait(async_wait_t* wait) override;
    zx_status_t CancelWait(async_wait_t* wait) override;
    zx_status_t PostTask(async_task_t* task) override;
    zx_status_t CancelTask(async_task_t* task) override;

    // TimerDispatcher operation implementation.
    void FireTimer() override;

    // Dispatches the next due task or wait. Returns true iff a message was
    // dispatched.
    bool DispatchNextDueMessage();

    // Whether there are any due tasks or waits.
    bool HasPendingWork();

    // Returns the deadline of the next posted task if one is pending; else
    // returns zx::time::infinite().
    zx::time GetNextTaskDueTime();

private:
    // Moves due tasks from |task_list_| to |due_list_|.
    void ExtractDueTasks();

    // Dispatches the next due task.
    void DispatchNextDueTask();

    // Dequeues from |port_| the next due packet. Must not be called if
    // |due_packet_| is already non-null.
    void ExtractNextDuePacket();

    // Dispatches all remaining posted waits and tasks, invoking their handlers
    // with status ZX_ERR_CANCELED.
    void Shutdown();

    // A reference to an external object that manages the current time and
    // and timers.
    TimeKeeper* const time_keeper_;

    // Port on which waits and timer expirations from |time_keeper_| are
    // signaled.
    zx::port port_;

    // The most recent packet dequeued from |port_|.
    fbl::unique_ptr<zx_port_packet_t> due_packet_;

    // Pending tasks, earliest deadline first.
    list_node_t task_list_;
    // Due tasks, earliest deadlines first.
    list_node_t due_list_;
    // Pending waits, most recently added first.
    list_node_t wait_list_;
};

} // namespace async
