// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TASK_QUEUE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TASK_QUEUE_H_

#include <condition_variable>
#include <functional>
#include <limits>
#include <list>
#include <mutex>
#include <utility>

namespace wlan {
namespace brcmfmac {

template <typename T>
class task_queue {
 public:
  using value_type = T;
  using container_type = std::list<T>;

  task_queue() = default;
  ~task_queue() = default;

  template <typename... Args>
  void emplace(Args&&... args) {
    container_type task;
    task.emplace_back(std::forward<Args>(args)...);
    std::lock_guard lock(tasks_mutex_);
    const bool should_signal = tasks_.empty();
    tasks_.splice(tasks_.end(), std::move(task));
    if (should_signal) {
      tasks_condvar_.notify_one();
    }
  }

  void splice(task_queue&& tasks) {
    container_type other_tasks;
    {
      std::lock_guard lock(tasks.tasks_mutex_);
      other_tasks = std::move(tasks.tasks_);
    }
    splice(std::move(other_tasks));
  }

  void splice(container_type&& tasks) {
    std::lock_guard lock(tasks_mutex_);
    const bool should_signal = tasks_.empty() && !tasks.empty();
    tasks_.splice(tasks_.end(), std::move(tasks));
    if (should_signal) {
      tasks_condvar_.notify_one();
    }
  }

  bool empty() const {
    std::lock_guard lock(tasks_mutex_);
    return tasks_.empty();
  }

  void clear() {
    std::lock_guard lock(tasks_mutex_);
    tasks_.clear();
  }

  // Run tasks on this task_queue instance, blocking until at least one task is available.  Returns
  // the number of tasks run.
  template <typename... Args>
  size_t run(Args&&... args) {
    container_type tasks;
    {
      std::unique_lock lock(tasks_mutex_);
      while (tasks_.empty()) {
        tasks_condvar_.wait(lock);
      }
      tasks.splice(tasks.end(), std::move(tasks_));
    }
    size_t run_count = 0;
    while (!tasks.empty()) {
      std::invoke(tasks.front(), std::forward<Args>(args)...);
      tasks.pop_front();
      ++run_count;
    }
    return run_count;
  }

  // Run tasks on this task_queue instance, or none if none are immediately available.  Does not
  // block.  Returns the number of tasks run.
  template <typename... Args>
  size_t try_run(Args&&... args) {
    container_type tasks;
    {
      std::lock_guard lock(tasks_mutex_);
      tasks.splice(tasks.end(), std::move(tasks_));
    }
    size_t run_count = 0;
    while (!tasks.empty()) {
      std::invoke(tasks.front(), std::forward<Args>(args)...);
      tasks.pop_front();
      ++run_count;
    }
    return run_count;
  }

 private:
  container_type tasks_;
  mutable std::mutex tasks_mutex_;
  std::condition_variable tasks_condvar_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TASK_QUEUE_H_
