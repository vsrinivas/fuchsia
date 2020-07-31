// Copyright 2020  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/log_stats_fetcher_impl.h"

#include <lib/syslog/cpp/macros.h>

#include <fstream>

namespace cobalt {

namespace {

static constexpr char kDefaultAllowlistFilePath[] =
    "/config/data/log_stats_component_allowlist.txt";

}  // namespace

// static
std::unordered_map<std::string, ComponentEventCode> LogStatsFetcherImpl::LoadAllowlist(
    const std::string& allowlist_path) {
  std::unordered_map<std::string, ComponentEventCode> result;
  std::ifstream allowlist_file_stream(allowlist_path);
  uint32_t component_code;
  while (allowlist_file_stream >> component_code) {
    std::string component_url;
    allowlist_file_stream >> component_url;
    result[component_url] = static_cast<ComponentEventCode>(component_code);
  }
  return result;
}

LogStatsFetcherImpl::LogStatsFetcherImpl(async_dispatcher_t* dispatcher,
                                         sys::ComponentContext* context)
    : LogStatsFetcherImpl(
          dispatcher,
          [context]() { return context->svc()->Connect<fuchsia::diagnostics::ArchiveAccessor>(); },
          LoadAllowlist(kDefaultAllowlistFilePath)) {}

LogStatsFetcherImpl::LogStatsFetcherImpl(
    async_dispatcher_t* dispatcher,
    fit::function<fuchsia::diagnostics::ArchiveAccessorPtr()> connector,
    std::unordered_map<std::string, ComponentEventCode> component_code_map)
    : executor_(dispatcher),
      archive_reader_(connector(), {"core/archivist:root/log_stats:*",
                                    "core/archivist:root/log_stats/by_component/*:error_logs"}),
      component_code_map_(std::move(component_code_map)) {
  // This establishes a baseline for error counts so that the next call to FetchMetrics() only
  // includes logs since the daemon started as opposed to since the  boot time. This is especially
  // important if the daemon had previously crashed and has been relaunched.
  FetchMetrics([](const Metrics& metrics) {});
}

void LogStatsFetcherImpl::FetchMetrics(MetricsCallback metrics_callback) {
  metrics_callback_ = std::move(metrics_callback);
  auto promise = archive_reader_.GetInspectSnapshot()
                     .and_then([this](std::vector<inspect::contrib::DiagnosticsData>& data_vector) {
                       this->OnInspectSnapshotReady(data_vector);
                     })
                     .or_else([](std::string& error) {
                       FX_LOGS(WARNING) << "Error while fetching log stats: " << error;
                     });
  executor_.schedule_task(std::move(promise));
}

void LogStatsFetcherImpl::OnInspectSnapshotReady(
    const std::vector<inspect::contrib::DiagnosticsData>& data_vector) {
  // Since we only asked for one component's inspect, there shouldn't be more
  // than one entry in the result.
  if (data_vector.size() != 1u) {
    FX_LOGS(ERROR) << "Expected 1 archive, received " << data_vector.size();
    return;
  }

  LogStatsFetcher::Metrics metrics;

  if (!CalculateTotalErrorCount(data_vector[0], &metrics.error_count)) {
    return;
  }

  if (!CalculatePerComponentErrorCount(data_vector[0], metrics.error_count,
                                       &metrics.per_component_error_count)) {
    return;
  }

  if (!CalculateKlogCount(data_vector[0], &metrics.klog_count)) {
    return;
  }

  metrics_callback_(metrics);
}

bool LogStatsFetcherImpl::CalculateTotalErrorCount(const inspect::contrib::DiagnosticsData& inspect,
                                                   uint64_t* result) {
  // Find the total error count.
  const rapidjson::Value& total_error_count_value =
      inspect.GetByPath({"root", "log_stats", "error_logs"});
  if (!total_error_count_value.IsUint64()) {
    FX_LOGS(ERROR) << "error_logs doesn't exist or is not a uint64";
    return false;
  }

  uint64_t total_error_count = total_error_count_value.GetUint64();
  // If the new error count is lower, Archivist  must have restarted, so
  // assume all errors are new.
  if (total_error_count < last_reported_total_error_count_) {
    *result = total_error_count;
  } else {
    *result = total_error_count - last_reported_total_error_count_;
  }
  FX_LOGS(DEBUG) << "Current total error count: " << total_error_count
                 << ", since last report: " << *result;
  last_reported_total_error_count_ = total_error_count;
  return true;
}

bool LogStatsFetcherImpl::CalculatePerComponentErrorCount(
    const inspect::contrib::DiagnosticsData& inspect, uint64_t total_error_count,
    std::unordered_map<ComponentEventCode, uint64_t>* result) {
  const rapidjson::Value& component_list = inspect.GetByPath({"root", "log_stats", "by_component"});
  if (!component_list.IsObject()) {
    FX_LOGS(ERROR) << "root/log_stats/by_component doesn't exist or is not an object";
    return false;
  }
  uint64_t tracked_error_count_diff = 0;
  for (auto& component_member : component_list.GetObject()) {
    std::string component_url = component_member.name.GetString();

    auto code_it = component_code_map_.find(component_url);
    // Ignore components not in the whitelist.
    if (code_it == component_code_map_.end())
      continue;

    ComponentEventCode component_code = code_it->second;

    if (!component_member.value.IsObject()) {
      FX_LOGS(ERROR) << "Component member must be an object: " << component_member.name.GetString();
      return false;
    }

    const auto& component_object = component_member.value.GetObject();

    auto error_logs_it = component_object.FindMember("error_logs");
    if (error_logs_it == component_object.MemberEnd()) {
      FX_LOGS(ERROR) << component_url << " does not have error_logs";
      return false;
    }

    if (!error_logs_it->value.IsUint64()) {
      FX_LOGS(ERROR) << "error_logs for component " << component_url << "is not uint64";
      return false;
    }

    uint64_t new_count = error_logs_it->value.GetUint64();
    uint64_t last_count = per_component_error_count_[component_code];

    // If the new error count is lower, the component must have restarted, so
    // assume all errors are new.
    uint64_t diff = new_count < last_count ? new_count : new_count - last_count;
    if (diff > 0) {
      FX_LOGS(DEBUG) << "Found " << diff << " new error logs for component " << component_url
                     << ", total is " << new_count;
    }
    (*result)[component_code] = diff;
    per_component_error_count_[component_code] = new_count;
    tracked_error_count_diff += diff;
  }
  uint64_t untracked_error_count;
  if (tracked_error_count_diff <= total_error_count) {
    untracked_error_count = total_error_count - tracked_error_count_diff;
  } else {
    FX_LOGS(ERROR) << "Error count in tracked components exceeds total error count";
    untracked_error_count = 0;
  }

  FX_LOGS(DEBUG) << "Untracked error logs since last report: " << untracked_error_count;
  (*result)[ComponentEventCode::Other] = untracked_error_count;
  return true;
}

bool LogStatsFetcherImpl::CalculateKlogCount(const inspect::contrib::DiagnosticsData& inspect,
                                             uint64_t* result) {
  const rapidjson::Value& new_klog_count_value =
      inspect.GetByPath({"root", "log_stats", "kernel_logs"});
  if (!new_klog_count_value.IsUint64()) {
    FX_LOGS(ERROR) << "kernel_logs doesn't exist or is not a uint64";
    return false;
  }
  uint64_t new_klog_count = new_klog_count_value.GetUint64();
  if (new_klog_count < last_reported_klog_count_) {
    *result = new_klog_count;
  } else {
    *result = new_klog_count - last_reported_klog_count_;
  }
  FX_LOGS(DEBUG) << "Current klog count: " << new_klog_count << ", since last report: " << *result;
  last_reported_klog_count_ = new_klog_count;
  return true;
}

}  // namespace cobalt
