// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/worker_pool.h"

#include <gtest/gtest.h>

#include <iostream>

#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/logging.h"

using namespace std::chrono_literals;

namespace debug_ipc {

namespace {

class TestWorkerPoolObserver : public WorkerPool::Observer {
 public:
  TestWorkerPoolObserver(std::mutex* mutex, std::condition_variable* cv)
      : mutex_(mutex), task_done_cv_(cv) {}

  void OnWorkerCreation() override;
  void OnWorkerExiting() override;

  void OnExecutingTask() override;
  void OnShutdown() override;

  void Reset();

  int workers_created() const { return workers_created_; }
  int workers_exited() const { return workers_exited_; }

  int tasks_executed() const { return tasks_executed_; }

  bool shutdown_called() const { return shutdown_called_; }
  bool all_tasks_done() const { return all_tasks_done_; }

  std::mutex& mutex() { return *mutex_; }
  std::condition_variable& task_done_cv() { return *task_done_cv_; }

 private:
  std::atomic<int> workers_created_ = 0;
  std::atomic<int> workers_exited_ = 0;
  std::atomic<int> tasks_executed_ = 0;

  std::atomic<bool> all_tasks_done_ = false;
  std::atomic<bool> shutdown_called_ = false;

  std::mutex* mutex_;
  std::condition_variable* task_done_cv_;
};

constexpr int kWorkerCount = 5;

void WaitForTasksToBeDone(TestWorkerPoolObserver* observer) {
  auto timeout = 1s;
  auto threshold = std::chrono::system_clock::now() + timeout;
  std::unique_lock<std::mutex> lock(observer->mutex());
  DEBUG_LOG(Test) << std::this_thread::get_id() << " Waiting for tasks.";
  observer->task_done_cv().wait_for(lock, timeout, [&observer, threshold] {
    // Either the tasks are done or we wait till the timeout.
    if (observer->all_tasks_done())
      return true;
    return threshold < std::chrono::system_clock::now();
  });
}

TEST(WorkerPool, PostTasks) {
  // Enable for debugging the test.
  // debug_ipc::SetDebugMode(true);

  std::mutex mutex;
  std::condition_variable task_done_cv;

  TestWorkerPoolObserver observer(&mutex, &task_done_cv);
  WorkerPool task_queue(kWorkerCount, &observer);

  std::vector<bool> tasks_called;
  for (int i = 0; i < kWorkerCount; i++) {
    std::lock_guard<std::mutex> lock(observer.mutex());
    tasks_called.push_back(false);

    bool posted = task_queue.PostTask([i, &tasks_called, &observer]() {
      std::lock_guard<std::mutex> guard(observer.mutex());
      tasks_called[i] = true;
      DEBUG_LOG(Test) << std::this_thread::get_id() << " First round: Task "
                      << i << " executed.";
    });
    ASSERT_TRUE(posted) << "First round: while posting task " << i;
  }

  // Since we're not running, no tasks should've been run.
  ASSERT_EQ(observer.workers_created(), 0);

  // Will run until all the tasks have been completed or timeout.
  task_queue.Run();
  WaitForTasksToBeDone(&observer);
  ASSERT_TRUE(observer.all_tasks_done());
  ASSERT_EQ(observer.tasks_executed(), kWorkerCount);

  // Should've created workers and not exited any.
  ASSERT_TRUE(observer.workers_created() > 0);
  ASSERT_EQ(observer.workers_exited(), 0);

  // Should've run all the tasks.
  ASSERT_EQ(observer.tasks_executed(), kWorkerCount);
  for (int i = 0; i < kWorkerCount; i++) {
    EXPECT_TRUE(tasks_called[i]) << "First Round: Task " << i << " not called.";
  }

  // We append some more tasks.
  observer.Reset();
  tasks_called.clear();
  for (int i = 0; i < kWorkerCount; i++) {
    std::lock_guard<std::mutex> lock(observer.mutex());
    tasks_called.push_back(false);

    bool posted = task_queue.PostTask([i, &tasks_called, &observer]() {
      std::lock_guard<std::mutex> lock(observer.mutex());
      tasks_called[i] = true;
      DEBUG_LOG(Test) << std::this_thread::get_id() << " Second round: Task "
                      << i << " executed.";
    });
    ASSERT_TRUE(posted) << "Second round: while posting task " << i;
  }

  WaitForTasksToBeDone(&observer);
  ASSERT_TRUE(observer.all_tasks_done());
  ASSERT_EQ(observer.tasks_executed(), kWorkerCount);

  // Should've run all the tasks.
  for (int i = 0; i < kWorkerCount; i++) {
    EXPECT_TRUE(tasks_called[i])
        << "Second Round: Task " << i << " not called.";
  }

  // No workers should've exited.
  ASSERT_EQ(observer.workers_exited(), 0);

  task_queue.Shutdown();

  // Should've join all the workers.
  EXPECT_TRUE(observer.shutdown_called());
  EXPECT_EQ(observer.workers_exited(), observer.workers_created());

  // Shouldn't be able to create tasks when shutdown.
  EXPECT_FALSE(task_queue.PostTask({}));
}

// TestWorkerPoolObserver
// -------------------------------------------------------

void TestWorkerPoolObserver::OnWorkerCreation() { workers_created_++; }

void TestWorkerPoolObserver::OnWorkerExiting() { workers_exited_++; }

void TestWorkerPoolObserver::OnExecutingTask() {
  std::lock_guard<std::mutex> lock(*mutex_);
  tasks_executed_++;
  DEBUG_LOG(Test) << std::this_thread::get_id()
                  << " executed task count: " << tasks_executed_;
  if (tasks_executed_ == kWorkerCount) {
    DEBUG_LOG(Test) << "All tasks are done!";
    all_tasks_done_ = true;
    task_done_cv_->notify_one();
  }
}

void TestWorkerPoolObserver::OnShutdown() { shutdown_called_ = true; };

void TestWorkerPoolObserver::Reset() {
  std::lock_guard<std::mutex> lock(*mutex_);
  tasks_executed_ = 0;
  all_tasks_done_ = false;
}

}  // namespace

}  // namespace debug_ipc
