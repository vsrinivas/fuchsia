// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/common/global_task_queue.h"

#include <lib/syslog/cpp/macros.h>

namespace media_audio {

void GlobalTaskQueue::Push(ThreadId id, fit::closure fn) {
  std::shared_ptr<Timer> next_timer;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    queue_.emplace_back(id, std::move(fn));
    next_timer = NextThreadToRun();
  }

  if (!next_timer) {
    // If this happens, id's Timer will not be notified which means the task
    // may never run (it will run only if the timer is notified some other way).
    FX_LOGS(WARNING) << "No timer registered for thread " << id << "; task may not run";
    return;
  }

  // Notify the next thread that there is work available.
  next_timer->SetEventBit();
}

void GlobalTaskQueue::RunForThread(ThreadId id) {
  std::shared_ptr<Timer> next_timer;
  bool warn_no_timer_for_task = false;

  // Run as many tasks as possible.
  for (Task* t = nullptr;;) {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (t) {
        // Pop the task we just ran.
        FX_CHECK(!queue_.empty()) << "Queue empty, possible use-after-free";
        queue_.pop_front();
      }
      if (queue_.empty()) {
        break;
      }
      t = &queue_.front();
      if (t->id != id && t->id != kAnyThreadId) {
        // This is not our task. Remember whose task it is so they can be notified.
        next_timer = NextThreadToRun();
        warn_no_timer_for_task = (next_timer == nullptr);
        break;
      }
      if (t->running) {
        // This must be a shared task which another thread is running.
        // That other thread will notify the next_timer when they are done.
        FX_CHECK(t->id == kAnyThreadId) << "Wrong thread is running tasks for tid=" << t->id;
        break;
      }
      t->running = true;
    }

    // Don't hold the lock while running the task.
    t->fn();
  }

  if (warn_no_timer_for_task) {
    // If this happens, id's Timer will not be notified which means the task
    // may never run (it will run only if the timer is notified some other way).
    FX_LOGS(WARNING) << "No timer registered for thread " << id << "; task may not run";
  }

  // Wake up the next timer.
  if (next_timer) {
    next_timer->SetEventBit();
  }
}

void GlobalTaskQueue::RegisterTimer(ThreadId id, std::shared_ptr<Timer> timer) {
  std::lock_guard<std::mutex> guard(mutex_);
  FX_CHECK(timers_.count(id) == 0) << "Registered multiple timers for Thread tid=" << id;
  FX_CHECK(id != kAnyThreadId) << "Cannot register a timer for kAnyThreadId";
  timers_[id] = std::move(timer);
}

void GlobalTaskQueue::UnregisterTimer(ThreadId id) {
  std::lock_guard<std::mutex> guard(mutex_);
  FX_CHECK(timers_.count(id) > 0) << "Timer not registered for Thread tid=" << id;
  timers_.erase(id);
}

std::shared_ptr<Timer> GlobalTaskQueue::NextThreadToRun() const {
  if (queue_.empty() || timers_.empty()) {
    return nullptr;
  }

  for (auto& task : queue_) {
    if (task.id == kAnyThreadId) {
      continue;
    }
    if (auto it = timers_.find(task.id); it != timers_.end()) {
      return it->second;
    }
    // Loop not registered for this ID.
    return nullptr;
  }

  // All tasks are kAnyThreadId, so they can run on any thread.
  // Pick a timer arbitrarily.
  return timers_.begin()->second;
}

}  // namespace media_audio
