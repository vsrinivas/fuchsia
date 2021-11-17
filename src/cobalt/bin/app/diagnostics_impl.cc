// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/diagnostics_impl.h"

#include <lib/inspect/cpp/inspect.h>

#include <chrono>

namespace cobalt {

DiagnosticsImpl::DiagnosticsImpl(inspect::Node node) : node_(std::move(node)) {
  // Diagnostics for sending observations to Clearcut.
  send_observations_ = node_.CreateChild("sending_observations");
  send_observations_successes_ = send_observations_.CreateInt("successes", 0);
  send_observations_errors_ = send_observations_.CreateInt("errors", 0);
  last_successful_send_time_ = send_observations_.CreateInt("last_success_time", 0);
  last_send_error_time_ = send_observations_.CreateInt("last_error_time", 0);
  last_send_error_code_ = send_observations_.CreateInt("last_error_code", 0);
  last_send_error_message_ = send_observations_.CreateString("last_error_message", "");
  last_send_error_details_ = send_observations_.CreateString("last_error_details", "");

  // Diagnostics for stored observations.
  stored_observations_ = node_.CreateChild("observations_stored");
  stored_observations_total_ = stored_observations_.CreateInt("total", 0);
  stored_observations_byte_count_ = stored_observations_.CreateInt("byte_count", 0);
  stored_observations_byte_count_limit_ = stored_observations_.CreateInt("byte_count_limit", 0);

  // Diagnostics for internal Cobalt metrics.
  internal_metrics_ = node_.CreateChild("internal_metrics");
  logger_calls_ = internal_metrics_.CreateChild("logger_calls");
  total_logger_calls_ = logger_calls_.CreateInt("total", 0);
  last_successful_logger_call_time_ = logger_calls_.CreateInt("last_successful_time", 0);
  logger_calls_per_project_node_ = logger_calls_.CreateChild("per_project");
  logger_calls_per_method_node_ = logger_calls_.CreateChild("per_method");
  disk_usage_ = internal_metrics_.CreateChild("disk_usage");
  disk_usage_per_storage_class_node_ = disk_usage_.CreateChild("per_storage_class");
}

void DiagnosticsImpl::SentObservationResult(const Status& status) {
  time_t current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  if (status.ok()) {
    send_observations_successes_.Add(1);
    last_successful_send_time_.Set(current_time);
  } else {
    send_observations_errors_.Add(1);
    last_send_error_time_.Set(current_time);
    last_send_error_code_.Set(static_cast<int64_t>(status.error_code()));
    last_send_error_message_.Set(status.error_message());
    last_send_error_details_.Set(status.error_details());
  }
}

void DiagnosticsImpl::ObservationStoreUpdated(
    const std::map<lib::ReportSpec, uint64_t>& num_obs_per_report, int64_t store_byte_count,
    int64_t max_store_bytes) {
  uint64_t total_observations = 0;
  for (const auto& [report_spec, count] : num_obs_per_report) {
    total_observations += count;
    std::string report_ids = "report_" + std::to_string(report_spec.customer_id) + "-" +
                             std::to_string(report_spec.project_id) + "-" +
                             std::to_string(report_spec.metric_id) + "-" +
                             std::to_string(report_spec.report_id);
    auto report_count = stored_observations_per_report_.find(report_ids);
    if (report_count != stored_observations_per_report_.end()) {
      report_count->second.Set(static_cast<int64_t>(count));
    } else {
      stored_observations_per_report_[report_ids] =
          stored_observations_.CreateInt(report_ids, static_cast<int64_t>(count));
    }
  }
  stored_observations_total_.Set(static_cast<int64_t>(total_observations));
  stored_observations_byte_count_.Set(store_byte_count);
  stored_observations_byte_count_limit_.Set(max_store_bytes);
}

DiagnosticsImpl::LoggerCalls* DiagnosticsImpl::FindOrCreateLoggerCallsForProject(
    const std::string& project) {
  const std::lock_guard<std::mutex> lock(logger_calls_per_project_lock_);
  auto logger_calls_for_project = logger_calls_per_project_.find(project);
  if (logger_calls_for_project != logger_calls_per_project_.end()) {
    return logger_calls_for_project->second.get();
  }
  auto logger_calls = std::make_unique<LoggerCalls>(&logger_calls_per_project_node_, project);
  DiagnosticsImpl::LoggerCalls* logger_calls_ptr = logger_calls.get();
  logger_calls_per_project_[project] = std::move(logger_calls);
  return logger_calls_ptr;
}

DiagnosticsImpl::LoggerCalls* DiagnosticsImpl::FindOrCreateLoggerCallsForMethod(
    int perProjectLoggerCallsMadeMetricDimensionLoggerMethod) {
  const std::lock_guard<std::mutex> lock(logger_calls_per_method_lock_);
  auto logger_calls_for_method =
      logger_calls_per_method_.find(perProjectLoggerCallsMadeMetricDimensionLoggerMethod);
  if (logger_calls_for_method != logger_calls_per_method_.end()) {
    return logger_calls_for_method->second.get();
  }
  auto logger_calls = std::make_unique<LoggerCalls>(
      &logger_calls_per_method_node_,
      "method_" + std::to_string(perProjectLoggerCallsMadeMetricDimensionLoggerMethod));
  DiagnosticsImpl::LoggerCalls* logger_calls_ptr = logger_calls.get();
  logger_calls_per_method_[perProjectLoggerCallsMadeMetricDimensionLoggerMethod] =
      std::move(logger_calls);
  return logger_calls_ptr;
}

void DiagnosticsImpl::LoggerCalled(int perProjectLoggerCallsMadeMetricDimensionLoggerMethod,
                                   const std::string& project) {
  time_t current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  total_logger_calls_.Add(1);
  last_successful_logger_call_time_.Set(current_time);
  DiagnosticsImpl::LoggerCalls* logger_calls_for_project =
      FindOrCreateLoggerCallsForProject(project);
  logger_calls_for_project->num_calls.Add(1);
  logger_calls_for_project->last_successful_time.Set(current_time);
  DiagnosticsImpl::LoggerCalls* logger_calls_for_method =
      FindOrCreateLoggerCallsForMethod(perProjectLoggerCallsMadeMetricDimensionLoggerMethod);
  logger_calls_for_method->num_calls.Add(1);
  logger_calls_for_method->last_successful_time.Set(current_time);
}

DiagnosticsImpl::DiskUsage* DiagnosticsImpl::FindOrCreateDiskUsage(int storageClass) {
  const std::lock_guard<std::mutex> lock(disk_usage_per_storage_class_lock_);
  auto disk_usage_for_storage_class = disk_usage_per_storage_class_.find(storageClass);
  if (disk_usage_for_storage_class != disk_usage_per_storage_class_.end()) {
    return disk_usage_for_storage_class->second.get();
  }
  auto disk_usage = std::make_unique<DiskUsage>(&disk_usage_per_storage_class_node_, storageClass);
  DiagnosticsImpl::DiskUsage* disk_usage_ptr = disk_usage.get();
  disk_usage_per_storage_class_[storageClass] = std::move(disk_usage);
  return disk_usage_ptr;
}

void DiagnosticsImpl::TrackDiskUsage(int storageClass, int64_t bytes, int64_t byte_limit) {
  DiagnosticsImpl::DiskUsage* disk_usage = FindOrCreateDiskUsage(storageClass);
  disk_usage->current_bytes.Set(bytes);
  if (bytes > disk_usage->max_bytes) {
    disk_usage->max_bytes = bytes;
    disk_usage->max_bytes_property.Set(bytes);
  }
  if (byte_limit > 0) {
    disk_usage->byte_limit.Set(byte_limit);
  }
}

}  // namespace cobalt
