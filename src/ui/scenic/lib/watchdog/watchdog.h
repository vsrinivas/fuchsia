// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_WATCHDOG_WATCHDOG_H_
#define SRC_UI_SCENIC_LIB_WATCHDOG_WATCHDOG_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/clock.h>
#include <lib/zx/timer.h>

#include <mutex>
#include <vector>
namespace scenic_impl {

class WatchdogImpl;

// A Watchdog class which monitors the aliveness of the async loop of thread
// that creates this object.
//
// Watchdog starts a new thread and lets the async loop run on that thread.
// Every |timeout_ms| milliseconds, the Watchdog class will post an "update"
// task on the async loop associated with |dispatcher|, and a "check" task
// on the watchdog thread async loop.
//
// If the "update" task is not executed, the whole process will crash and
// the stack trace will be printed to klog.
class Watchdog {
 public:
  // Watchdog constructor.
  // Params:
  // - |timeout_ms|: maximum timeout allowed for a thread to be unresponsive.
  // - |dispatcher|: the async dispatcher the user wants Watchdog to monitor.
  Watchdog(uint64_t timeout_ms, async_dispatcher_t* dispatcher);
  ~Watchdog();

 private:
  async::Loop loop_;
  std::unique_ptr<WatchdogImpl> watchdog_impl_;
};

class WatchdogImpl {
 public:
  // Params:
  // - |timeout_ms|: The time between two consecutive timer tasks. If watched
  //       thread is unresponsive during this time, process will crash.
  // - |watchdog_dispatcher|: async dispatcher of watchdog thread's async loop.
  // - |watched_thread_dispatcher|: async dispatcher of the watched thread's
  //       async loop.
  // - |run_update_fn|: A closure which updates the watchdog state, which is
  //       executed by *watchdog thread* every |timeout_ms| ms.
  // - |check_update_fn|: A function which should check if the watchdog state
  //       is updated.
  //       Returns false if |run_update_fn| was not called during the
  //       past |timeout_ms| ms; Otherwise returns true.
  WatchdogImpl(uint64_t timeout_ms, async_dispatcher_t* watchdog_dispatcher,
               async_dispatcher_t* watched_thread_dispatcher, fit::closure run_update_fn,
               fit::function<bool(void)> check_update_fn);
  ~WatchdogImpl();

  // Initialize the Watchdog, post PostUpdateTask() onto watched thread's
  // async loop, and post HandleTimer() task onto watchdog thread's async
  // loop.
  void Initialize();

  // Finalize the Watchdog, cancel all pending tasks.
  void Finalize();

 private:
  // Post the update task to watchdog's async loop.
  // This function runs on watched thread's async loop.
  void PostUpdateTask();

  // Run update_() to update the watchdog status.
  // This function runs on watchdog's async loop.
  void RunUpdate();

  // Run check_update_() to check if the watched process is active; and then
  // post a new pair of PostUpdateTask() and HandleTimer() tasks to
  // corresponding dispatchers.
  // This function runs on watchdog's async loop.
  void HandleTimer();

  // Helper method used by Initialize() and HandleTimer().
  // Posts tasks to watched and watchdog threads.
  void PostTasks();

  zx::time last_update_timestamp_ = zx::time(0);

  // Time between two consecutive check_update_() calls.
  zx::duration timeout_ = zx::duration(0);

  // Number of times the watchdog polls the watched thread between two
  // consecutive timer handler functions.
  //
  // We set this value to 3, i.e. there will be (timeout_ / 4) ms between
  // two consecutive updates, or update and check.
  // This avoids frequent polling and it ensures it would be no more
  // than |timeout_| ms before we detect an unresponsive thread.
  constexpr static size_t kPollingNum = 3;

  bool initialized_ = false;
  bool finalized_ = false;
  std::mutex mutex_;

  async_dispatcher_t* watchdog_dispatcher_;
  async_dispatcher_t* watched_thread_dispatcher_;

  fit::closure run_update_fn_;
  fit::function<bool(void)> check_update_fn_;

  using PostUpdateTaskClosureMethod =
      async::TaskClosureMethod<WatchdogImpl, &WatchdogImpl::PostUpdateTask>;
  std::vector<std::unique_ptr<PostUpdateTaskClosureMethod>> post_update_tasks_;
  async::TaskClosureMethod<WatchdogImpl, &WatchdogImpl::RunUpdate> run_update_task_{this};
  async::TaskClosureMethod<WatchdogImpl, &WatchdogImpl::HandleTimer> handle_timer_task_{this};
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_WATCHDOG_WATCHDOG_H_
