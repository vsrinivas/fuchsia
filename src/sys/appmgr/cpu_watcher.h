// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_CPU_WATCHER_H_
#define SRC_SYS_APPMGR_CPU_WATCHER_H_

#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/value_list.h>
#include <lib/inspect/cpp/vmo/types.h>
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
        measurements_(top_node_.CreateChild("measurements")),
        task_count_value_(top_node_.CreateInt("task_count", 0)),
        process_times_(top_node_.CreateExponentialIntHistogram(
            "process_time_ns", kProcessTimeFloor, kProcessTimeStep, kProcessTimeMultiplier,
            kProcessTimeBuckets)),
        root_(measurements_.CreateChild("root"), std::move(job), max_samples),
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

  // An individual measurement.
  struct Measurement {
    // Root node for measurement values.
    inspect::Node node;
    // Holder for values stored under the node.
    inspect::ValueList values;
  };

  // A task that can be measured.
  class Task {
   public:
    Task(inspect::Node node, zx::job job, size_t max_samples)
        : node_(std::move(node)),
          samples_(node_.CreateChild("@samples")),
          job_(std::move(job)),
          max_samples_(max_samples){};

    Task(Task* parent, zx::job job, const std::string& name, size_t max_samples)
        : Task(parent->node_.CreateChild(name), std::move(job), max_samples) {}

    // Add a measurement to this task.
    void add_measurement(zx_time_t time, zx_duration_t cpu, zx_duration_t queue) {
      Measurement ret{.node = samples_.CreateChild(std::to_string(next_id++))};
      ret.node.CreateInt("timestamp", time, &ret.values);
      ret.node.CreateInt("cpu_time", cpu, &ret.values);
      ret.node.CreateInt("queue_time", queue, &ret.values);

      measurements_.emplace_back(std::move(ret));
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

   private:
    // The node that roots this task's output.
    inspect::Node node_;

    // The node that roots this task's sample output.
    inspect::Node samples_;

    // The job to sample.
    zx::job job_;

    // The maximum number of samples to store for this task.
    const size_t max_samples_;

    // Deque of measurements.
    std::deque<Measurement> measurements_;

    // Map of children for this task.
    std::map<std::string, std::unique_ptr<Task>> children_;

    // Unique id counter.
    size_t next_id = 0;
  };

  std::mutex mutex_;
  inspect::Node top_node_;
  inspect::Node measurements_;
  inspect::IntProperty task_count_value_;
  inspect::ExponentialIntHistogram process_times_;

  size_t task_count_ __TA_GUARDED(mutex_) = 1;  // 1 for root_
  Task root_ __TA_GUARDED(mutex_);

  const size_t max_samples_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CpuWatcher);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_CPU_WATCHER_H_
