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
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <deque>
#include <map>
#include <string>
#include <thread>

#include <src/lib/fxl/macros.h>
#include <src/sys/appmgr/component_controller_impl.h>

namespace component {

// Virtual class to support CPU stats injection for testing
class StatsReader {
 public:
  virtual ~StatsReader() = default;
  // Returns ZX_OK when task runtime is written to *info. Otherwise the value of *info is unchanged.
  virtual zx_status_t GetCpuStats(zx_info_task_runtime_t* info) = 0;
};

// Gets stats from a real job
class JobStatsReader final : public StatsReader {
 public:
  ~JobStatsReader() override = default;
  explicit JobStatsReader(zx::job job) : job_(std::move(job)) {}
  zx_status_t GetCpuStats(zx_info_task_runtime_t* info) override {
    return job_.get_info(ZX_INFO_TASK_RUNTIME, info, sizeof(*info), nullptr, nullptr);
  }

 private:
  zx::job job_;
};

// Configures the CpuWatcher. num_cpus and get_time can be substituted for testing.
struct CpuWatcherParameters {
  // How many CPU cores the system has
  size_t num_cpus = zx_system_get_num_cpus();
  // How often samples are taken
  zx::duration sample_period;
  // A function that will be called to fetch monotonic time
  std::function<zx::time()> get_time = [] { return zx::clock::get_monotonic(); };
};

// Watch CPU usage for tasks on the system.
//
// The CpuWatcher periodically samples all CPU usage registered tasks and exposes it in an Inspect
// hierarchy.
class CpuWatcher {
 public:
  // Create a new CpuWatcher that exposes CPU data under the given inspect node. The given job
  // appears as the root of the hierarchy.
  CpuWatcher(inspect::Node node, CpuWatcherParameters parameters,
             std::unique_ptr<StatsReader> stats_reader, size_t max_samples = kDefaultMaxSamples)
      : parameters_(std::move(parameters)),
        top_node_(std::move(node)),
        measurements_(
            top_node_.CreateLazyNode("measurements", [this] { return PopulateInspector(); })),
        task_count_value_(top_node_.CreateInt("task_count", 0)),
        process_times_(top_node_.CreateExponentialIntHistogram(
            "process_time_ns", kProcessTimeFloor, kProcessTimeStep, kProcessTimeMultiplier,
            kProcessTimeBuckets)),
        root_(std::move(stats_reader), max_samples, cpp17::nullopt /*histogram*/,
              parameters.get_time()),
        total_node_(top_node_.CreateChild("@total")),
        recent_cpu_usage_(
            top_node_.CreateLazyNode("recent_usage", [this] { return PopulateRecentUsage(); })),
        histograms_node_(top_node_.CreateChild("histograms")),
        max_samples_(max_samples) {}

  // Add a task to this watcher by instance path.
  // job: The job to sample CPU runtime from.
  void AddTask(const InstancePath& instance_path, std::unique_ptr<StatsReader> stats_reader);

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
    Task(std::unique_ptr<StatsReader> stats_reader, size_t max_samples,
         cpp17::optional<inspect::LinearUintHistogram> histogram, zx::time time)
        : stats_reader_(std::move(stats_reader)),
          max_samples_(max_samples),
          // Histogram for values [0..100]. 0 in the lower overflow bucket, 100 in the upper.
          histogram_(std::move(histogram)),
          previous_histogram_timestamp_(time.get()),
          previous_cpu_(0) {}

    // Add a measurement to this task's histogram.
    void AddMeasurementToHistogram(zx::time time, zx_duration_t cpu_time,
                                   const CpuWatcherParameters& parameters,
                                   inspect::LinearUintHistogram* histogram_to_use) {
      if (histogram_to_use == nullptr) {
        return;
      }
      auto time_value = time.get();
      auto elapsed_time = zx_time_sub_time(time_value, previous_histogram_timestamp_);
      previous_histogram_timestamp_ = time_value;
      // Don't publish confusing or misleading values from too-short measurement period.
      if (elapsed_time < parameters.sample_period.get() * 9 / 10) {
        return;
      }
      // Elapsed time * the number of cores
      auto available_core_time = elapsed_time * parameters.num_cpus;
      if (available_core_time != 0) {
        // Multiply by 100 to get percent. Add available_core_time-1 to compute ceil().
        auto cpu_numerator = cpu_time * 100 + available_core_time - 1;
        histogram_to_use->Insert(cpu_numerator / available_core_time);
      }
    }

    // Add a measurement to this task's list of measurements.
    void AddMeasurementToList(zx::time time, zx_duration_t cpu_time, zx_duration_t queue_time) {
      measurements_.emplace_back(
          Measurement{.timestamp = time.get(), .cpu_time = cpu_time, .queue_time = queue_time});
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
      return stats_reader_ != nullptr || !measurements_.empty() || !children_.empty();
    }

    std::map<std::string, std::unique_ptr<Task>>& children() { return children_; }
    const std::map<std::string, std::unique_ptr<Task>>& children() const { return children_; }
    const std::deque<Measurement>& measurements() const { return measurements_; }
    std::unique_ptr<StatsReader>& stats_reader() { return stats_reader_; }
    inspect::LinearUintHistogram* histogram() {
      return histogram_ ? &(histogram_.value()) : nullptr;
    }
    // Takes and records a new measurement for this task. A copy of the measurement is returned if
    // one was taken. Parent must not be null.
    cpp17::optional<Measurement> Measure(const zx::time& timestamp,
                                         const CpuWatcherParameters& parameters, Task* parent);

   private:
    std::unique_ptr<StatsReader> stats_reader_;

    // The maximum number of samples to store for this task.
    const size_t max_samples_;

    // Deque of measurements.
    std::deque<Measurement> measurements_;

    // Inspect histogram of CPU stat percentages.
    // Multiple tasks may occur with different koid's but a shared moniker, for example
    // due to restart. Use a histogram-for-all-koid's stored in the parent Task.
    cpp17::optional<inspect::LinearUintHistogram> histogram_;

    // Map of children for this task.
    std::map<std::string, std::unique_ptr<Task>> children_;

    // Time of previous CPU sample or creation of Task instance, or 0 if invalid.
    zx_time_t previous_histogram_timestamp_;

    zx_duration_t previous_cpu_;
  };

  mutable std::mutex mutex_;
  CpuWatcherParameters parameters_;
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
  inspect::Node histograms_node_;
  Measurement most_recent_total_ = {}, second_most_recent_total_ = {};

  const size_t max_samples_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CpuWatcher);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_CPU_WATCHER_H_
