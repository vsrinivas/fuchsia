// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/worker_pool.h"

#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/logging.h"

namespace debug_ipc {

namespace {

// Useful for logging.
auto Preamble() { return std::this_thread::get_id(); }

// Unlocks on constructor, locks on destructor.
class UnlockGuard {
 public:
  using LockType = std::unique_lock<std::mutex>;

  UnlockGuard(LockType& lock) : lock_(&lock) {
    FXL_DCHECK(lock_->owns_lock());
    lock_->unlock();
  }

  ~UnlockGuard() {
    FXL_DCHECK(!lock_->owns_lock());
    lock_->lock();
  }

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(UnlockGuard);

 private:
  LockType* lock_;  // Not owning. Must outlive.
};

}  // namespace

class WorkerPool::Worker {
 public:
  Worker(WorkerPool* pool) : owning_pool_(pool) {}

  void Run() {
    FXL_DCHECK(owning_pool_);
    FXL_DCHECK(!thread_.joinable());  // Unstarted threads are not joinable.
    thread_ = std::thread(&WorkerPool::ThreadLoop, owning_pool_);
  }

  void Join() {
    if (thread_.joinable())
      thread_.join();
  }

 private:
  // We can have a back ref because the queue owns this object.
  WorkerPool* owning_pool_ = nullptr;
  std::thread thread_;
};

WorkerPool::WorkerPool(int max_workers, Observer* observer)
    : max_workers_(max_workers), observer_(observer) {}

WorkerPool::~WorkerPool() { Shutdown(); }

bool WorkerPool::PostTask(Task&& task) {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (shutting_down_)
      return false;

    tasks_.push_back(std::move(task));

    if (!running_)
      return true;

    if (ShouldCreateWorker()) {
      CreateWorker(&lock);
      return true;
    }
  }

  SignalWork();
  return true;
}

void WorkerPool::Run() {
  DEBUG_LOG(WorkerPool) << Preamble() << " Running the queue.";
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (running_)
      return;
    running_ = true;

    // If there are no posted tasks, there is nothing to do yet.
    if (tasks_.empty())
      return;

    // If there are no available workers, we create a first one.
    if (ShouldCreateWorker()) {
      CreateWorker(&lock);
      return;
    }
  }

  // We signal a worker. That worker will wake up other workers if needed.
  SignalWork();
}

bool WorkerPool::ShouldCreateWorker() {
  // The task will create a new worker only with the following criteria:
  return !shutting_down_ &&                 // 1. The loop is not shutting down.
         workers_.size() < max_workers_ &&  // 2. We can create more workers.
         !creating_worker_ &&               // 3. A worker is not being created.
         waiting_workers_ == 0 &&           // 4. There are no idle workers.
         !tasks_.empty();                   // 5. There is actual work to do.
}


void WorkerPool::CreateWorker(std::unique_lock<std::mutex>* lock) {
  // NOTE: |lock| is held.
  FXL_DCHECK(lock);
  FXL_DCHECK(!creating_worker_);
  creating_worker_ = true;
  DEBUG_LOG(WorkerPool) << Preamble() << " Creating a worker.";

  // We can create the worker outside the lock.
  std::unique_ptr<Worker> worker;
  {
    UnlockGuard unlock_guard(*lock);
    worker = std::make_unique<Worker>(this);
    if (observer_)
      observer_->OnWorkerCreation();
    worker->Run();
  }

  // NOTE: |lock| is taken here.
  workers_.push_back(std::move(worker));
}

void WorkerPool::ThreadLoop() {
  DEBUG_LOG(WorkerPool) << Preamble() << " Starting as new thread.";
  std::unique_lock<std::mutex> lock(mutex_);

  // Only one thread must be created at the same time.
  FXL_DCHECK(creating_worker_) << " on thread " << std::this_thread::get_id();
  creating_worker_ = false;

  // Only the shutdown thread should be waiting on this CV.
  worker_created_cv_.notify_one();

  while (true) {
    // If we're shutting down. We're out.
    if (shutting_down_)
      break;

    // If there are no new tasks. We simply wait for work.
    if (tasks_.empty()) {
      waiting_workers_++;
      work_available_cv_.wait(lock, [this]() {
        // If we're shutting down, we get out of this CV.
        // Otherwise, we see if there are pending tasks.
        if (shutting_down_)
          return true;
        return !tasks_.empty();
      });
      waiting_workers_--;
    }

    // If we we're woken and we are shutting down, we need to go out.
    if (shutting_down_)
      break;

    // We obtain the task.
    auto task = std::move(tasks_.front());
    tasks_.pop_front();

    // See if we need another worker.
    FXL_DCHECK(lock.owns_lock());
    if (ShouldCreateWorker())
      CreateWorker(&lock);

    {
      UnlockGuard unlock_guard(lock);

      // There may be more work available, so we wake up another thread.
      // This is a just in case call.
      SignalWork();

      // Finally we do the task outside the lock.
      task();
      if (observer_)
        observer_->OnExecutingTask();
    }
  }

  DEBUG_LOG(WorkerPool) << Preamble() << " Exiting.";
  if (observer_)
    observer_->OnWorkerExiting();
}

void WorkerPool::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutting_down_)
      return;
    shutting_down_ = true;
  }

  if (observer_)
    observer_->OnShutdown();

  // We join all the pending workers and go out.
  JoinAllWorkers();
}

void WorkerPool::JoinAllWorkers() {
  {
    // If there is a thread being created, we need it to be safely created
    // before we can join them.
    std::unique_lock<std::mutex> lock(mutex_);
    FXL_DCHECK(shutting_down_);

    // We signal any sleeping workers.
    SignalAllWorkers();

    if (creating_worker_) {
      DEBUG_LOG(WorkerPool) << "Waiting for worker creation before exiting.";
      worker_created_cv_.wait(lock, [this]() { return !creating_worker_; });
    }
  }

  // At this point we know all workers are running or have exited, so we can
  // safely join them.
  for (auto& worker : workers_) {
    worker->Join();
  }
}

void WorkerPool::SignalWork() { work_available_cv_.notify_one(); }

void WorkerPool::SignalAllWorkers() { work_available_cv_.notify_all(); }

}  // namespace debug_ipc
