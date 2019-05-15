// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/lib/containers/cpp/circular_deque.h"

namespace debug_ipc {

// Multi-threaded arbitrary task queue.
//
// This queue is meant for tasks that are independent of each other (ie. they
// don't need ordering between each other). The queue will spawn up workers as
// needed and will shut them down upon destruction.
//
// NOTE: When shutting down, the pool will wait for all workers to be done.
//       Before that, it will prevent any new work being started but any tasks
//       that are being run at that moment will finish and block, either upon
//       calling Shutdown or on the destructor.
//
// NOTE2: The thread annotations are (mostly) commented out because the C++
//        condition variables requires a taken std::unique_lock<std::mutex> to
//        work, but clang's thread annotation analysis does not recognize them
//        as valid, as opposing std::lock_guard<std::mutex>. This means that
//        this queue won't compile if the actual thread annotations were set in
//        place.
class WorkerPool {
 public:
  using Task = std::function<void()>;

  class Worker;

  // Used to inject behaviour to the queue for testing purposes.
  // Should be null in production.
  class Observer {
   public:
    virtual ~Observer() = default;

    virtual void OnWorkerCreation() = 0;
    virtual void OnWorkerExiting() = 0;
    virtual void OnExecutingTask() = 0;
    virtual void OnShutdown() = 0;
  };

  WorkerPool(int max_workers, Observer* = nullptr);
  ~WorkerPool();

  // Starts the queue. Before this, posting tasks won't create workers.
  void Run();

  // Returns whether the task was successfully posted.
  bool PostTask(Task&&);

  // Calls join underneath.
  void Shutdown();

 private:
  // The actual loop that a workers runs on another thread.
  void ThreadLoop();

  bool ShouldCreateWorker();  // REQUIRES(mutex_)

  // This requires |lock| to be taken, but will unlock it for the actual worker
  // thread creation, and retake it after that work is done.
  void CreateWorker(std::unique_lock<std::mutex>* lock);  // REQUIRES(mutex_)

  void SignalWork() FXL_LOCKS_EXCLUDED(mutex_);
  void SignalAllWorkers() FXL_LOCKS_EXCLUDED(mutex_);

  // Will join on all the threads. Handles the case where one is being created.
  void JoinAllWorkers() FXL_LOCKS_EXCLUDED(mutex_);

  size_t max_workers_ = 0;
  std::vector<std::unique_ptr<Worker>> workers_;  // GUARDED_BY(mutex_)
  ::containers::circular_deque<Task> tasks_;      // GUARDED_BY(mutex_)

  // Counters.
  int waiting_workers_ = 0;   // GUARDED_BY(mutex_)

  // State machine.

  // Cannot use thread safety analysis because we use std::unique_lock.
  bool running_  = false;         // GUARDED_BY(mutex_)
  bool shutting_down_ = false;    // GUARDED_BY(mutex_)

  // Whether we're creating a worker.
  // The new worker, upon startup, will switch off this flag.
  volatile bool creating_worker_  = false;  // GUARDED_BY(mutex_)

  mutable std::mutex mutex_;

  std::condition_variable worker_created_cv_;   // REQUIRES(mutex_)
  std::condition_variable work_available_cv_;   // REQUIRES(mutex_)

  Observer* observer_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(WorkerPool);
};

}  // namespace debug_ipc
