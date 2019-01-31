// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_SCHEDULER_H_
#define LIB_FIT_SCHEDULER_H_

#include <map>
#include <queue>
#include <utility>

#include "promise.h"

namespace fit {
namespace subtle {

// Keeps track of runnable and suspended tasks.
// This is a low-level building block for implementing executors.
// For a concrete implementation, see |fit::single_threaded_executor|.
//
// Instances of this object are not thread-safe.  Its client is responsible
// for providing all necessary synchronization.
class scheduler final {
public:
    using task_queue = std::queue<pending_task>;
    using ref_count = uint32_t;

    scheduler();
    ~scheduler();

    // Adds a task to the runnable queue.
    //
    // Preconditions:
    // - |task| must be non-empty
    void schedule_task(pending_task task);

    // Obtains a new ticket with a ref-count of |initial_refs|.
    // The executor must eventually call |finalize_ticket()| to update the
    // state of the ticket.
    //
    // Preconditions:
    // - |initial_refs| must be at least 1
    suspended_task::ticket obtain_ticket(ref_count initial_refs = 1);

    // Updates a ticket after one run of a task's continuation according
    // to the state of the task after its run.  The executor must call this
    // method after calling |obtain_ticket()| to indicate the disposition of
    // the task for which the ticket was obtained.
    //
    // Passing an empty |task| indicates that the task has completed so it
    // does not need to be resumed.
    //
    // Passing a non-empty |task| indicates that the task returned a pending
    // result and may need to be suspended depending on the current state
    // of the ticket.
    // - If the ticket has already been resumed, moves |task| into the
    //   runnable queue.
    // - Otherwise, if the ticket still has a non-zero ref-count, moves |task|
    //   into the suspended task table.
    // - Otherwise, considers the task abandoned and the caller retains
    //   ownership of |task|.
    //
    // Preconditions:
    // - |task| must be non-null (may be empty)
    // - the ticket must not have already been finalized
    void finalize_ticket(suspended_task::ticket ticket, pending_task* task);

    // Increments the ticket's ref-count.
    //
    // Preconditions:
    // - the ticket's ref-count must be non-zero (positive)
    void duplicate_ticket(suspended_task::ticket ticket);

    // Decrements the ticket's ref-count.
    //
    // If the task's ref-count reaches 0 and has an associated task that
    // has not already been resumed, returns the associated task back
    // to the caller.
    // Otherwise, returns an empty task.
    //
    // Preconditions:
    // - the ticket's ref-count must be non-zero (positive)
    pending_task release_ticket(suspended_task::ticket ticket);

    // Resumes a task and decrements the ticket's ref-count.
    //
    // If the ticket has an associated task that has not already been resumed,
    // moves its associated task to the runnable queue and returns true.
    // Otherwise, returns false.
    //
    // Preconditions:
    // - the ticket's ref-count must be non-zero (positive)
    bool resume_task_with_ticket(suspended_task::ticket ticket);

    // Takes all tasks in the runnable queue.
    //
    // Preconditions:
    // - |tasks| must be non-null and empty
    void take_runnable_tasks(task_queue* tasks);

    // Takes all remaining tasks, regardless of whether they are runnable
    // or suspended.
    //
    // This operation is useful when shutting down an executor.
    //
    // Preconditions:
    // - |tasks| must be non-null and empty
    void take_all_tasks(task_queue* tasks);

    // Returns true if there are any runnable tasks.
    bool has_runnable_tasks() const { return !runnable_tasks_.empty(); }

    // Returns true if there are any suspended tasks that have yet to
    // be resumed.
    bool has_suspended_tasks() const { return suspended_task_count_ > 0; }

    // Returns true if there are any tickets that have yet to be finalized,
    // released, or resumed.
    bool has_outstanding_tickets() const { return !tickets_.empty(); }

    scheduler(const scheduler&) = delete;
    scheduler(scheduler&&) = delete;
    scheduler& operator=(const scheduler&) = delete;
    scheduler& operator=(scheduler&&) = delete;

private:
    struct ticket_record {
        ticket_record(ref_count initial_refs)
            : ref_count(initial_refs), was_resumed(false) {}

        // The current reference count.
        ref_count ref_count;

        // True if the task has been resumed using |resume_task_with_ticket()|.
        bool was_resumed;

        // The task is initially empty when the ticket is obtained.
        // It is later set to non-empty if the task needs to be suspended when
        // the ticket is finalized.  It becomes empty again when the task
        // is moved into the runnable queue, released, or taken.
        pending_task task;
    };
    using ticket_map = std::map<suspended_task::ticket, ticket_record>;

    task_queue runnable_tasks_;
    ticket_map tickets_;
    uint64_t suspended_task_count_ = 0;
    suspended_task::ticket next_ticket_ = 1;
};

} // namespace subtle
} // namespace fit

#endif // LIB_FIT_SCHEDULER_H_
