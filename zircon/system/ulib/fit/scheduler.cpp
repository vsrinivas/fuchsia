// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Can't compile this for Zircon userspace yet since libstdc++ isn't available.
#ifndef FIT_NO_STD_FOR_ZIRCON_USERSPACE

#include <lib/fit/scheduler.h>

#include <map>
#include <queue>
#include <utility>

namespace fit {
namespace subtle {

scheduler::scheduler() = default;

scheduler::~scheduler() = default;

void scheduler::schedule_task(pending_task task) {
    assert(task);
    runnable_tasks_.push(std::move(task));
}

suspended_task::ticket scheduler::obtain_ticket(uint32_t initial_refs) {
    suspended_task::ticket ticket = next_ticket_++;
    tickets_.emplace(ticket, ticket_record(initial_refs));
    return ticket;
}

void scheduler::finalize_ticket(suspended_task::ticket ticket,
                                pending_task* task) {
    auto it = tickets_.find(ticket);
    assert(it != tickets_.end());
    assert(!it->second.task);
    assert(it->second.ref_count > 0);
    assert(task);

    it->second.ref_count--;
    if (!*task) {
        // task already finished
    } else if (it->second.was_resumed) {
        // task immediately became runnable
        runnable_tasks_.push(std::move(*task));
    } else if (it->second.ref_count > 0) {
        // task remains suspended
        it->second.task = std::move(*task);
        suspended_task_count_++;
    } // else, task was abandoned and caller retains ownership of it
    if (it->second.ref_count == 0) {
        tickets_.erase(it);
    }
}

void scheduler::duplicate_ticket(suspended_task::ticket ticket) {
    auto it = tickets_.find(ticket);
    assert(it != tickets_.end());
    assert(it->second.ref_count > 0);

    it->second.ref_count++;
    assert(it->second.ref_count != 0); // did we really make 4 billion refs?!
}

pending_task scheduler::release_ticket(suspended_task::ticket ticket) {
    auto it = tickets_.find(ticket);
    assert(it != tickets_.end());
    assert(it->second.ref_count > 0);

    it->second.ref_count--;
    if (it->second.ref_count == 0) {
        pending_task task = std::move(it->second.task);
        if (task) {
            assert(suspended_task_count_ > 0);
            suspended_task_count_--;
        }
        tickets_.erase(it);
        return task;
    }
    return pending_task();
}

bool scheduler::resume_task_with_ticket(suspended_task::ticket ticket) {
    auto it = tickets_.find(ticket);
    assert(it != tickets_.end());
    assert(it->second.ref_count > 0);

    bool did_resume = false;
    it->second.ref_count--;
    if (!it->second.was_resumed) {
        it->second.was_resumed = true;
        if (it->second.task) {
            did_resume = true;
            assert(suspended_task_count_ > 0);
            suspended_task_count_--;
            runnable_tasks_.push(std::move(it->second.task));
        }
    }
    if (it->second.ref_count == 0) {
        tickets_.erase(it);
    }
    return did_resume;
}

void scheduler::take_runnable_tasks(task_queue* tasks) {
    assert(tasks && tasks->empty());
    runnable_tasks_.swap(*tasks);
}

void scheduler::take_all_tasks(task_queue* tasks) {
    assert(tasks && tasks->empty());

    runnable_tasks_.swap(*tasks);
    if (suspended_task_count_ > 0) {
        for (auto& item : tickets_) {
            if (item.second.task) {
                assert(suspended_task_count_ > 0);
                suspended_task_count_--;
                tasks->push(std::move(item.second.task));
            }
        }
    }
}

} // namespace subtle
} // namespace fit

#endif // FIT_NO_STD_FOR_ZIRCON_USERSPACE
