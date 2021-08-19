// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_watcher.h"

#include <lib/stdcompat/optional.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>

#include <memory>
#include <mutex>
#include <type_traits>

namespace component {

namespace {
constexpr char kTimestamp[] = "timestamp";
constexpr char kCpuTime[] = "cpu_time";
constexpr char kQueueTime[] = "queue_time";
constexpr char kPreviousCpuTime[] = "previous_cpu_time";
constexpr char kPreviousQueueTime[] = "previous_queue_time";
constexpr char kPreviousTimestamp[] = "previous_timestamp";
constexpr char kRecentCpuTime[] = "recent_cpu_time";
constexpr char kRecentQueueTime[] = "recent_queue_time";
constexpr char kRecentTimestamp[] = "recent_timestamp";
}  // namespace

void CpuWatcher::AddTask(const InstancePath& instance_path,
                         std::unique_ptr<StatsReader> stats_reader) {
  TRACE_DURATION("appmgr", "CpuWatcher::AddTask", "name",
                 instance_path.empty() ? "" : instance_path.back());

  std::lock_guard<std::mutex> lock(mutex_);

  Task* cur_task = &root_;
  Task* parent_task = &root_;
  unsigned long path_length = instance_path.size(), path_position = 0;
  std::ostringstream histogram_name;
  for (const auto& part : instance_path) {
    histogram_name << part;
    auto it = cur_task->children().find(part);
    if (it == cur_task->children().end()) {
      bool inserted;
      cpp17::optional<inspect::LinearUintHistogram> histogram;
      // The leaves of this tree are koids. We want one histogram for all koids that may occur
      // (for example if a component is restarted) so we'll create that at the parent-of-leaf Task.
      if (path_position == path_length - 2) {
        histogram = cpp17::make_optional(histograms_node_.CreateLinearUintHistogram(
            histogram_name.str(), 1 /*floor*/, 1 /*step_size*/, 99 /*buckets*/));
      } else {
        histogram = cpp17::nullopt;
      }
      std::tie(it, inserted) = cur_task->children().emplace(
          part, std::make_unique<Task>(Task(nullptr /* stats_reader */, max_samples_,
                                            std::move(histogram), parameters_.get_time())));
      task_count_ += 1;
      task_count_value_.Set(task_count_);
    }
    parent_task = cur_task;
    cur_task = it->second.get();
    histogram_name << '/';
    path_position++;
  }
  cur_task->stats_reader() = std::move(stats_reader);
  // Measure tasks on creation.
  cur_task->Measure(parameters_.get_time(), parameters_, parent_task);
}

void CpuWatcher::RemoveTask(const InstancePath& instance_path) {
  TRACE_DURATION("appmgr", "CpuWatcher::RemoveTask", "name",
                 instance_path.empty() ? "" : instance_path.back());
  std::lock_guard<std::mutex> lock(mutex_);

  Task* cur_task = &root_;
  Task* parent_task = &root_;
  for (const auto& part : instance_path) {
    auto it = cur_task->children().find(part);
    if (it == cur_task->children().end()) {
      return;
    }
    parent_task = cur_task;
    cur_task = it->second.get();
  }

  // Measure before resetting the job, so we get final runtime stats.
  cur_task->Measure(parameters_.get_time(), parameters_, parent_task);
  const auto& measurements = cur_task->measurements();
  auto it = measurements.rbegin();
  if (it != measurements.rend()) {
    exited_cpu_ += it->cpu_time;
    exited_queue_ += it->queue_time;
  }
  cur_task->stats_reader().reset();
}

void CpuWatcher::Measure() {
  zx::time start = parameters_.get_time();
  {
    TRACE_DURATION("appmgr", "CpuWatcher::Measure", "num_tasks", task_count_);
    std::lock_guard<std::mutex> lock(mutex_);
    Measurement overall{
        .timestamp = start.get(), .cpu_time = exited_cpu_, .queue_time = exited_queue_};
    // We store measurement-lists in the leaves, but histograms in the parents of the leaves.
    // to_measure is pair <parent, child> treating root as the parent of itself.
    std::vector<std::pair<CpuWatcher::Task*, CpuWatcher::Task*>> to_measure;
    to_measure.reserve(task_count_);
    to_measure.push_back(std::make_pair(&root_, &root_));

    for (size_t current_offset = 0; current_offset < to_measure.size(); current_offset++) {
      auto& children = to_measure[current_offset].second->children();
      for (auto& child : children) {
        to_measure.push_back(std::make_pair(to_measure[current_offset].second, child.second.get()));
      }
    }

    auto stamp = parameters_.get_time();

    for (auto cur_iter = to_measure.rbegin(); cur_iter != to_measure.rend(); ++cur_iter) {
      auto cur = *cur_iter;

      auto measurement = cur.second->Measure(stamp, parameters_, cur.first);
      if (measurement.has_value()) {
        overall.cpu_time += measurement.value().cpu_time;
        overall.queue_time += measurement.value().queue_time;
      }

      for (auto it = cur.second->children().begin(); it != cur.second->children().end();) {
        if (!it->second->is_alive()) {
          it = cur.second->children().erase(it);
          task_count_ -= 1;
          task_count_value_.Set(task_count_);
        } else {
          ++it;
        }
      }
    }

    inspect::ValueList value_list;
    inspect::Node total_measurement =
        total_node_.CreateChild(std::to_string(next_total_measurement_id_++));
    total_measurement.CreateInt(kTimestamp, overall.timestamp, &value_list);
    total_measurement.CreateInt(kCpuTime, overall.cpu_time, &value_list);
    total_measurement.CreateInt(kQueueTime, overall.queue_time, &value_list);
    value_list.emplace(std::move(total_measurement));
    total_measurements_.emplace_back(std::move(value_list));
    while (total_measurements_.size() > max_samples_) {
      total_measurements_.pop_front();
    }

    second_most_recent_total_ = most_recent_total_;
    most_recent_total_ = overall;
  }
  process_times_.Insert((parameters_.get_time() - start).get());
}

cpp17::optional<CpuWatcher::Measurement> CpuWatcher::Task::Measure(
    const zx::time& timestamp, const CpuWatcherParameters& parameters, Task* parent) {
  if (stats_reader_) {
    TRACE_DURATION("appmgr", "CpuWatcher::Task::Measure");
    zx_info_task_runtime_t info;
    if (ZX_OK == stats_reader_->GetCpuStats(&info)) {
      TRACE_DURATION("appmgr", "CpuWatcher::Task::Measure::AddMeasurement");
      AddMeasurementToList(timestamp, info.cpu_time, info.queue_time);
      ZX_DEBUG_ASSERT(parent != nullptr);
      if (parent != nullptr) {
        auto histogram = parent->histogram();
        if (histogram) {
          AddMeasurementToHistogram(timestamp, info.cpu_time - previous_cpu_, parameters,
                                    histogram);
        }
      }
      previous_cpu_ = info.cpu_time;
    }
    return cpp17::make_optional(Measurement{
        .timestamp = timestamp.get(), .cpu_time = info.cpu_time, .queue_time = info.queue_time});
  } else {
    TRACE_DURATION("appmgr", "CpuWatcher::Task::Measure:Rotate");
    rotate();
    return cpp17::nullopt;
  }
}

fpromise::promise<inspect::Inspector> CpuWatcher::PopulateInspector() const {
  TRACE_DURATION("appmgr", "CpuWatcher::PopulateInspector");
  std::lock_guard<std::mutex> lock(mutex_);

  inspect::Inspector inspector(inspect::InspectSettings{.maximum_size = 2 * 1024 * 1024});

  auto stats_node = inspector.GetRoot().CreateChild("@inspect");
  auto size = stats_node.CreateUint("current_size", 0);
  auto max_size = stats_node.CreateUint("maximum_size", 0);
  auto dynamic_links = stats_node.CreateUint("dynamic_links", 0);

  struct WorkEntry {
    const char* name;
    const Task* task;
    inspect::Node* parent;
  };
  std::vector<WorkEntry> work_stack;
  work_stack.push_back(WorkEntry{.name = "root", .task = &root_, .parent = &inspector.GetRoot()});

  while (!work_stack.empty()) {
    auto entry = work_stack.back();
    work_stack.pop_back();

    auto node = std::make_unique<inspect::Node>(entry.parent->CreateChild(entry.name));
    inspect::Node* node_ptr = node.get();
    inspector.emplace(std::move(node));
    if (!entry.task->measurements().empty()) {
      inspect::Node samples = node_ptr->CreateChild("@samples");
      size_t next_id = 0;
      for (const auto& measurement : entry.task->measurements()) {
        auto sample = samples.CreateChild(std::to_string(next_id++));
        sample.CreateInt(kTimestamp, measurement.timestamp, &inspector);
        sample.CreateInt(kCpuTime, measurement.cpu_time, &inspector);
        sample.CreateInt(kQueueTime, measurement.queue_time, &inspector);
        inspector.emplace(std::move(sample));
      }
      inspector.emplace(std::move(samples));
    }

    for (const auto& child : entry.task->children()) {
      work_stack.push_back(
          WorkEntry{.name = child.first.c_str(), .task = &*child.second, .parent = node_ptr});
    }
  }

  // Include stats about the Inspector that is being exposed.
  // This data can be used to determine if the measurement inspector is full.
  auto stats = inspector.GetStats();
  size.Set(stats.size);
  max_size.Set(stats.maximum_size);
  dynamic_links.Set(stats.dynamic_child_count);

  inspector.emplace(std::move(stats_node));
  inspector.emplace(std::move(size));
  inspector.emplace(std::move(max_size));
  inspector.emplace(std::move(dynamic_links));

  return fpromise::make_ok_promise(std::move(inspector));
}

fpromise::promise<inspect::Inspector> CpuWatcher::PopulateRecentUsage() const {
  TRACE_DURATION("appmgr", "CpuWatcher::PopulateRecentUsage");
  std::lock_guard<std::mutex> lock(mutex_);

  inspect::Inspector inspector(inspect::InspectSettings{.maximum_size = 4096});

  inspector.GetRoot().CreateInt(kPreviousCpuTime, second_most_recent_total_.cpu_time, &inspector);
  inspector.GetRoot().CreateInt(kPreviousQueueTime, second_most_recent_total_.queue_time,
                                &inspector);
  inspector.GetRoot().CreateInt(kPreviousTimestamp, second_most_recent_total_.timestamp,
                                &inspector);

  inspector.GetRoot().CreateInt(kRecentCpuTime, most_recent_total_.cpu_time, &inspector);
  inspector.GetRoot().CreateInt(kRecentQueueTime, most_recent_total_.queue_time, &inspector);
  inspector.GetRoot().CreateInt(kRecentTimestamp, most_recent_total_.timestamp, &inspector);

  return fpromise::make_ok_promise(std::move(inspector));
}

}  // namespace component
