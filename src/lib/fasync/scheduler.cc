// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/scheduler.h>

#include <map>
#include <queue>
#include <utility>

namespace fasync {
namespace subtle {

suspended_task::ticket scheduler::obtain_ticket(uint32_t initial_refs) {
  suspended_task::ticket ticket = next_ticket_++;
  auto result = tickets_.emplace(ticket, ticket_record(initial_refs));
  assert(result.second);  // Insertion took place; no previously existing ticket
  static_cast<void>(result);
  return ticket;
}

void scheduler::finalize_ticket(suspended_task::ticket ticket, pending_task& task) {
  auto it = tickets_.find(ticket);
  assert(it != tickets_.end());
  assert(!it->second.task);
  assert(it->second.ref_count > 0);

  it->second.ref_count--;
  if (it->second.was_resumed) {
    // task immediately became runnable
    runnable_tasks_.push(std::move(task));
  } else if (it->second.ref_count > 0) {
    // task remains suspended
    it->second.task = std::move(task);
    suspended_task_count_++;
  }  // else, task was abandoned and caller retains ownership of it
  if (it->second.ref_count == 0) {
    tickets_.erase(it);
  }
}

void scheduler::duplicate_ticket(suspended_task::ticket ticket) {
  auto it = tickets_.find(ticket);
  assert(it != tickets_.end());
  assert(it->second.ref_count > 0);

  it->second.ref_count++;
  assert(it->second.ref_count != 0);  // did we really make 4 billion refs?!
}

cpp17::optional<pending_task> scheduler::release_ticket(suspended_task::ticket ticket) {
  auto it = tickets_.find(ticket);
  assert(it != tickets_.end());
  assert(it->second.ref_count > 0);

  it->second.ref_count--;
  if (it->second.ref_count == 0) {
    cpp17::optional<pending_task> task = std::move(it->second.task);
    it->second.task = cpp17::nullopt;
    if (task.has_value()) {
      assert(suspended_task_count_ > 0);
      suspended_task_count_--;
    }
    tickets_.erase(it);
    return task;
  }
  return cpp17::nullopt;
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
      runnable_tasks_.push(std::move(*it->second.task));
      it->second.task = cpp17::nullopt;
    }
  }
  if (it->second.ref_count == 0) {
    tickets_.erase(it);
  }
  return did_resume;
}

scheduler::task_queue scheduler::take_runnable_tasks() {
  scheduler::task_queue tasks;
  runnable_tasks_.swap(tasks);
  return tasks;
}

scheduler::task_queue scheduler::take_all_tasks() {
  scheduler::task_queue tasks;
  runnable_tasks_.swap(tasks);
  if (suspended_task_count_ > 0) {
    for (auto& item : tickets_) {
      if (item.second.task) {
        assert(suspended_task_count_ > 0);
        suspended_task_count_--;
        tasks.push(std::move(*item.second.task));
        item.second.task = cpp17::nullopt;
      }
    }
  }
  return tasks;
}

}  // namespace subtle
}  // namespace fasync
