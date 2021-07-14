// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_CPU_WATCHER_H_
#define SRC_SYS_APPMGR_CPU_WATCHER_H_

#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/value_list.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/stdcompat/optional.h>
#include <lib/zx/job.h>
#include <lib/zx/vmo.h>
#include <zircon/threads.h>

#include <deque>
#include <map>
#include <string>
#include <thread>

#include <src/lib/fxl/macros.h>
#include <src/sys/appmgr/component_controller_impl.h>

namespace component {

// Watch CPU usage for tasks on the system.
//
// The CpuWatcher periodically samples all CPU usage registered tasks and exposes it in an Inspect
// hierarchy.
class CpuWatcher {
 public:
  // Create a new CpuWatcher that exposes CPU data under the given inspect node. The given job
  // appears as the root of the hierarchy.
  CpuWatcher(inspect::Node node, zx::job job, size_t max_samples = kDefaultMaxSamples)
      : top_node_(std::move(node)),
        measurements_(
            top_node_.CreateLazyNode("measurements", [this] { return PopulateInspector(); })),
        task_count_value_(top_node_.CreateInt("task_count", 0)),
        process_times_(top_node_.CreateExponentialIntHistogram(
            "process_time_ns", kProcessTimeFloor, kProcessTimeStep, kProcessTimeMultiplier,
            kProcessTimeBuckets)),
        root_(std::move(job), max_samples),
        total_node_(top_node_.CreateChild("@total")),
        recent_cpu_usage_(
            top_node_.CreateLazyNode("recent_usage", [this] { return PopulateRecentUsage(); })),
        max_samples_(max_samples) {}

  // Add a task to this watcher by instance path.
  // job: The job to sample CPU runtime from.
  void AddTask(const InstancePath& instance_path, zx::job job);

  // Remove a task by instance path.
  void RemoveTask(const InstancePath& instance_path);

  // Execute a measurement at the current time.
  void Measure();

 private:
  static constexpr int64_t kProcessTimeFloor = 1000;
  static constexpr int64_t kProcessTimeStep = 1000;
  static constexpr int64_t kProcessTimeMultiplier = 2;
  static constexpr int64_t kProcessTimeBuckets = 16;
  static constexpr size_t kDefaultMaxSamples = 60;

  fpromise::promise<inspect::Inspector> PopulateInspector() const;
  fpromise::promise<inspect::Inspector> PopulateRecentUsage() const;

  // An individual measurement.
  struct Measurement {
    zx_time_t timestamp;
    zx_duration_t cpu_time;
    zx_duration_t queue_time;
  };

  // A task that can be measured.
  class Task {
   public:
    Task(zx::job job, size_t max_samples) : job_(std::move(job)), max_samples_(max_samples){};

    // Add a measurement to this task.
    void add_measurement(zx_time_t time, zx_duration_t cpu, zx_duration_t queue) {
      measurements_.emplace_back(
          Measurement{.timestamp = time, .cpu_time = cpu, .queue_time = queue});
      while (measurements_.size() > max_samples_) {
        measurements_.pop_front();
      }
    }

    // Rotate measurements if not empty. This is used when a task is already destroyed to ensure
    // that we still rotate measurements outside the window.
    void rotate() {
      if (!measurements_.empty()) {
        measurements_.pop_front();
      }
    }

    bool is_alive() const {
      // Keep a task around if we will either take measurements from it, or we have existing
      // measurements.
      return job_.is_valid() || !measurements_.empty() || !children_.empty();
    }

    zx::job& job() { return job_; }
    std::map<std::string, std::unique_ptr<Task>>& children() { return children_; }
    const std::map<std::string, std::unique_ptr<Task>>& children() const { return children_; }
    const std::deque<Measurement>& measurements() const { return measurements_; }

    // Takes and records a new measurement for this task. A copy of the measurement is returned if
    // one was taken.
    cpp17::optional<Measurement> Measure(const zx::time& timestamp);

   private:
    // The job to sample.
    zx::job job_;

    // The maximum number of samples to store for this task.
    const size_t max_samples_;

    // Deque of measurements.
    std::deque<Measurement> measurements_;

    // Map of children for this task.
    std::map<std::string, std::unique_ptr<Task>> children_;
  };

  mutable std::mutex mutex_;
  inspect::Node top_node_;
  inspect::LazyNode measurements_;
  inspect::IntProperty task_count_value_;
  inspect::ExponentialIntHistogram process_times_;

  size_t task_count_ __TA_GUARDED(mutex_) = 1;  // 1 for root_
  Task root_ __TA_GUARDED(mutex_);

  // Total CPU and queue time of exited tasks. Used to ensure those values are not lost when
  // calculating overall CPU usage on the system.
  zx_duration_t exited_cpu_ __TA_GUARDED(mutex_) = 0;
  zx_duration_t exited_queue_ __TA_GUARDED(mutex_) = 0;
  inspect::Node total_node_;
  size_t next_total_measurement_id_ __TA_GUARDED(mutex_) = 0;
  std::deque<inspect::ValueList> total_measurements_ __TA_GUARDED(mutex_);

  inspect::LazyNode recent_cpu_usage_;
  Measurement most_recent_total_ = {}, second_most_recent_total_ = {};

  const size_t max_samples_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CpuWatcher);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_CPU_WATCHER_H_
