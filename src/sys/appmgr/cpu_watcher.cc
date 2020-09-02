// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_watcher.h"

#include <lib/fit/optional.h>
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

void CpuWatcher::AddTask(const InstancePath& instance_path, zx::job job) {
  TRACE_DURATION("appmgr", "CpuWatcher::AddTask", "name",
                 instance_path.empty() ? "" : instance_path.back());

  std::lock_guard<std::mutex> lock(mutex_);

  Task* cur_task = &root_;
  for (const auto& part : instance_path) {
    auto it = cur_task->children().find(part);
    if (it == cur_task->children().end()) {
      bool inserted;
      std::tie(it, inserted) =
          cur_task->children().emplace(part, std::make_unique<Task>(Task(zx::job(), max_samples_)));
      task_count_ += 1;
      task_count_value_.Set(task_count_);
    }
    cur_task = it->second.get();
  }
  cur_task->job() = std::move(job);
  // Measure tasks on creation.
  cur_task->Measure(zx::clock::get_monotonic());
}

void CpuWatcher::RemoveTask(const InstancePath& instance_path) {
  TRACE_DURATION("appmgr", "CpuWatcher::RemoveTask", "name",
                 instance_path.empty() ? "" : instance_path.back());
  std::lock_guard<std::mutex> lock(mutex_);

  Task* cur_task = &root_;
  for (const auto& part : instance_path) {
    auto it = cur_task->children().find(part);
    if (it == cur_task->children().end()) {
      return;
    }

    cur_task = it->second.get();
  }

  // Measure before resetting the job, so we get final runtime stats.
  cur_task->Measure(zx::clock::get_monotonic());
  const auto& measurements = cur_task->measurements();
  auto it = measurements.rbegin();
  if (it != measurements.rend()) {
    exited_cpu_ += it->cpu_time;
    exited_queue_ += it->queue_time;
  }
  cur_task->job().reset();
}

void CpuWatcher::Measure() {
  zx::time start = zx::clock::get_monotonic();
  {
    TRACE_DURATION("appmgr", "CpuWatcher::Measure", "num_tasks", task_count_);
    std::lock_guard<std::mutex> lock(mutex_);
    Measurement overall{
        .timestamp = start.get(), .cpu_time = exited_cpu_, .queue_time = exited_queue_};
    std::vector<Task*> to_measure;
    to_measure.push_back(&root_);
    to_measure.reserve(task_count_);

    for (size_t current_offset = 0; current_offset < to_measure.size(); current_offset++) {
      auto& children = to_measure[current_offset]->children();
      for (auto& child : children) {
        to_measure.push_back(child.second.get());
      }
    }

    auto stamp = zx::clock::get_monotonic();

    for (auto cur_iter = to_measure.rbegin(); cur_iter != to_measure.rend(); ++cur_iter) {
      auto* cur = *cur_iter;

      auto measurement = cur->Measure(stamp);
      if (measurement.has_value()) {
        overall.cpu_time += measurement.value().cpu_time;
        overall.queue_time += measurement.value().queue_time;
      }

      for (auto it = cur->children().begin(); it != cur->children().end();) {
        if (!it->second->is_alive()) {
          it = cur->children().erase(it);
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
  process_times_.Insert((zx::clock::get_monotonic() - start).get());
}

fit::optional<CpuWatcher::Measurement> CpuWatcher::Task::Measure(const zx::time& timestamp) {
  if (job().is_valid()) {
    TRACE_DURATION("appmgr", "CpuWatcher::Task::Measure");
    zx_info_task_runtime_t info;
    if (ZX_OK == job().get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr)) {
      TRACE_DURATION("appmgr", "CpuWatcher::Task::Measure::AddMeasurement");
      add_measurement(timestamp.get(), info.cpu_time, info.queue_time);
    }
    return fit::make_optional(Measurement{
        .timestamp = timestamp.get(), .cpu_time = info.cpu_time, .queue_time = info.queue_time});
  } else {
    TRACE_DURATION("appmgr", "CpuWatcher::Task::Measure:Rotate");
    rotate();
    return fit::nullopt;
  }
}

fit::promise<inspect::Inspector> CpuWatcher::PopulateInspector() const {
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

  return fit::make_ok_promise(std::move(inspector));
}

fit::promise<inspect::Inspector> CpuWatcher::PopulateRecentUsage() const {
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

  return fit::make_ok_promise(std::move(inspector));
}

}  // namespace component
